#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Preferences.h>
#include <VL53L0X.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <stddef.h>
#include "probe_logic.h"

// Copy production secrets.h here locally (gitignored), or fill the two IDs
// in a local secrets.h. No network credentials are used or persisted.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

constexpr uint8_t ENABLE_PIN = 1, SDA_PIN = 5, SCL_PIN = 6, RX_PIN = 44;
constexpr uint16_t SAMPLE_CAP = 640, EVENT_CAP = 64;
constexpr uint32_t FORMAT = 0x42505201;
enum EventCode : uint16_t { PowerOn = 1, PowerOff, ScanStart, FirstByte,
  ValidChip, BadFrame, ScanEnd, SensorError, StaleBytes, SleepError };
struct Sample { uint32_t ms; uint16_t mm; uint16_t flags; };
struct Event { uint32_t ms; uint16_t code; uint16_t value; uint32_t aux; };
struct Capture {
  uint32_t format, runId, mode, started, ended, trigger;
  uint32_t samplesSeen, invalidSamples, maxGap, scans, recognized, validOther;
  uint32_t bytes, badFrames, staleBytes, onMs, sleepMs, sleepCalls, sleepErrors;
  uint32_t sampleDrops, eventDrops, sensorFailed, aborted;
  uint16_t sampleCount, eventCount;
  Sample samples[SAMPLE_CAP];
  Event events[EVENT_CAP];
  uint32_t hash;
};
static_assert(sizeof(Capture) < 7000, "Keep the one-shot NVS capture small");

VL53L0X tof;
HardwareSerial reader(1);
Preferences storage;
Probe::Controller control;
Probe::Parser parser;
Capture capture{};
bool storageReady = false, running = false, powered = false, triggered = false, parked = false;
bool sampled = false, gotByte = false, captureInRam = false, tofReady = false;
uint32_t lastSample = 0, poweredAt = 0, seenGeneration = 0;
Sample previousSample{};
bool havePreviousSample = false;
Probe::End seenEnd = Probe::End::None;

void event(uint16_t code, uint16_t value = 0, uint32_t aux = 0) {
  if (capture.eventCount == EVENT_CAP) { ++capture.eventDrops; return; }
  capture.events[capture.eventCount++] = Event{millis(), code, value, aux};
}
void setPower(bool on) {
  if (powered == on) return;
  const uint32_t now = millis();
  digitalWrite(ENABLE_PIN, on ? HIGH : LOW);
  if (on) poweredAt = now;
  else capture.onMs += uint32_t(now - poweredAt);
  powered = on;
  event(on ? PowerOn : PowerOff, digitalRead(ENABLE_PIN), control.generation);
}
void syncScan() {
  if (control.generation != seenGeneration) {
    seenGeneration = control.generation; seenEnd = Probe::End::None;
    parser.reset(); gotByte = false;
    // Discard only the pre-window backlog, before enabling a cold reader.
    // Do not discard/delay early bytes from the newly powered window.
    const int backlog = reader.available();
    for (int i = 0; i < backlog; ++i) reader.read();
    capture.staleBytes += backlog;
    if (backlog) event(StaleBytes, static_cast<uint16_t>(backlog));
    ++capture.scans;
    event(ScanStart, 0, seenGeneration);
  }
  setPower(control.powerWanted());
  if (!control.scanning && control.end != seenEnd) {
    seenEnd = control.end;
    event(ScanEnd, static_cast<uint16_t>(seenEnd), uint32_t(millis() - control.scanStart));
    parser.reset();
  }
}
void serviceReader() {
  control.tick(millis()); syncScan();
  for (unsigned i = 0; i < 96 && reader.available(); ++i) {
    const char c = static_cast<char>(reader.read());
    control.tick(millis()); syncScan();
    if (!control.scanning) { ++capture.staleBytes; continue; }
    ++capture.bytes;
    if (!gotByte) { gotByte = true; event(FirstByte, 0, uint32_t(millis() - control.scanStart)); }
    char chip[16] = {};
    const Probe::Parse result = parser.feed(c, chip);
    if (result == Probe::Parse::Invalid) { ++capture.badFrames; event(BadFrame); }
    if (result != Probe::Parse::Valid) continue;
    const uint16_t cat = CAT_1_CHIP_RAW[0] && strcmp(chip, CAT_1_CHIP_RAW) == 0 ? 1 :
                         CAT_2_CHIP_RAW[0] && strcmp(chip, CAT_2_CHIP_RAW) == 0 ? 2 : 0;
    event(ValidChip, cat, uint32_t(millis() - control.scanStart));
    if (!cat) { ++capture.validOther; continue; }
    if (control.accept(millis())) ++capture.recognized;
    syncScan(); // GPIO goes LOW immediately on registered success in gated modes.
  }
}
void printHelp() {
  Serial.println("battery_probe v1; NO Sheets uploads; no visit-duration claims");
  Serial.println("a: arm gated+awake; b: arm gated+light-sleep (recommended)");
  Serial.println("c: arm ALWAYS-ON reference, 60s only; i: arm 60s idle sleep test");
  Serial.println("a/b wait <=15min for first blockage, then record 60s.");
  Serial.println("Arming does NOT start: disconnect USB, then power from battery.");
  Serial.println("d: dump last capture; x: cancel next arm; h: help");
  Serial.printf("registered cats=%u; ON/OFF pin=%u; threshold=200mm; period=100ms\n",
    unsigned(CAT_1_CHIP_RAW[0] != 0) + unsigned(CAT_2_CHIP_RAW[0] != 0), ENABLE_PIN);
  if (storageReady) Serial.printf("next_mode=%u interrupted_run=%lu\n",
    storage.getUChar("next", 0), static_cast<unsigned long>(storage.getUInt("inflight", 0)));
}
void dumpCapture() {
  if (!captureInRam) {
    if (!storageReady || storage.getBytesLength("capture") != sizeof(capture) ||
        storage.getBytes("capture", &capture, sizeof(capture)) != sizeof(capture) ||
        capture.format != FORMAT || capture.sampleCount > SAMPLE_CAP || capture.eventCount > EVENT_CAP ||
        capture.hash != Probe::checksum(&capture, offsetof(Capture, hash))) {
      Serial.println("NO_VALID_CAPTURE (never interpret this as a successful test)"); return;
    }
  }
  Serial.println("BEGIN_CAPTURE; times are uptime ms, NOT wall clock or visit duration");
  Serial.printf("interrupted_run=%lu (nonzero: latest attempt may NOT be the saved capture)\n",
    storageReady ? (unsigned long)storage.getUInt("inflight", 0) : 0UL);
  Serial.printf("run=%lu,mode=%lu,start=%lu,end=%lu,trigger=%lu,aborted=%lu,sensor_failed=%lu\n",
    (unsigned long)capture.runId, (unsigned long)capture.mode, (unsigned long)capture.started,
    (unsigned long)capture.ended, (unsigned long)capture.trigger,
    (unsigned long)capture.aborted, (unsigned long)capture.sensorFailed);
  Serial.printf("samples=%lu,invalid=%lu,max_gap_ms=%lu,scans=%lu,recognized=%lu,other_valid=%lu\n",
    (unsigned long)capture.samplesSeen, (unsigned long)capture.invalidSamples,
    (unsigned long)capture.maxGap, (unsigned long)capture.scans,
    (unsigned long)capture.recognized, (unsigned long)capture.validOther);
  Serial.printf("commanded_on_ms=%lu,sleep_ms=%lu,sleep_calls=%lu,sleep_errors=%lu,bytes=%lu,bad_frames=%lu,stale_bytes=%lu,sample_drops=%lu,event_drops=%lu\n",
    (unsigned long)capture.onMs, (unsigned long)capture.sleepMs, (unsigned long)capture.sleepCalls,
    (unsigned long)capture.sleepErrors, (unsigned long)capture.bytes, (unsigned long)capture.badFrames,
    (unsigned long)capture.staleBytes, (unsigned long)capture.sampleDrops, (unsigned long)capture.eventDrops);
  Serial.println("S,uptime_ms,distance_mm,flags(bit0=valid bit1=blocked bit2=GPIO_on)");
  for (uint16_t i = 0; i < capture.sampleCount; ++i) {
    const Sample &s = capture.samples[i];
    Serial.printf("S,%lu,%u,%u\n", (unsigned long)s.ms, s.mm, s.flags);
  }
  Serial.println("E,uptime_ms,code,value,aux");
  for (uint16_t i = 0; i < capture.eventCount; ++i) {
    const Event &e = capture.events[i];
    Serial.printf("E,%lu,%u,%u,%lu\n", (unsigned long)e.ms, e.code, e.value, (unsigned long)e.aux);
  }
  Serial.println("END_CAPTURE");
}
void finishRun(bool aborted = false) {
  control.cancel(); control.mode = Probe::Mode::None; syncScan();
  capture.ended = millis(); capture.aborted = aborted;
  capture.hash = Probe::checksum(&capture, offsetof(Capture, hash));
  running = false; captureInRam = true; parked = true;
  reader.end(); // No peripheral work remains before parked Light-sleep.
  const bool saved = storageReady && storage.putBytes("capture", &capture, sizeof(capture)) == sizeof(capture);
  if (saved && storage.remove("inflight")) Serial.println("CAPTURE_SAVED; use d after USB reconnect/reset");
  else Serial.println("SAVE_FAILED_OR_INFLIGHT_NOT_CLEARED; RAM dump only; do not disconnect");
}
void startRun(Probe::Mode mode) {
  capture = Capture{}; capture.format = FORMAT;
  capture.runId = esp_random(); if (!capture.runId) capture.runId = 1;
  capture.mode = static_cast<uint32_t>(mode);
  // Keep previous capture intact if this run is interrupted or saving fails.
  if (storage.putUInt("inflight", capture.runId) != sizeof(uint32_t)) {
    Serial.println("CANNOT_MARK_RUN; test refused"); return;
  }
  WiFi.mode(WIFI_OFF); // No connection, NTP, web server, camera, or upload task.
  reader.setRxBufferSize(512);
  reader.begin(9600, SERIAL_8N1, RX_PIN, -1); // Receive before enabling; TX remains unassigned.
  Wire.begin(SDA_PIN, SCL_PIN);
  tof.setTimeout(100);
  tofReady = tof.init() && tof.setMeasurementTimingBudget(20000);
  capture.started = millis(); lastSample = capture.started;
  running = true;
  if (!tofReady) { capture.sensorFailed = 1; event(SensorError); finishRun(true); return; }
  control = Probe::Controller{}; control.mode = mode;
  triggered = mode == Probe::Mode::ReferenceOn || mode == Probe::Mode::IdleSleep;
  if (triggered) capture.trigger = capture.started;
  syncScan();
}
void command(char c) {
  if (c == 'h') { printHelp(); return; }
  if (c == 'd') { dumpCapture(); return; }
  if (!storageReady) { Serial.println("NVS_UNAVAILABLE; cannot arm"); return; }
  if (c == 'x') { storage.remove("next"); printHelp(); return; }
  const uint8_t mode = c == 'a' ? 1 : c == 'b' ? 2 : c == 'c' ? 3 : c == 'i' ? 4 : 0;
  if (!mode) return;
  if (storage.putUChar("next", mode) != sizeof(uint8_t)) { Serial.println("ARM_FAILED"); return; }
  Serial.printf("ARMED mode=%u for NEXT BOOT; disconnect USB before connecting boost power\n", mode);
}
void setup() {
  pinMode(ENABLE_PIN, OUTPUT); digitalWrite(ENABLE_PIN, LOW);
  Serial.begin(115200);
  WiFi.mode(WIFI_OFF);
  storageReady = storage.begin("battery-probe", false);
  const uint8_t next = storageReady ? storage.getUChar("next", 0) : 0;
  if (next >= 1 && next <= 4 && storage.remove("next")) startRun(static_cast<Probe::Mode>(next));
  else printHelp();
}
void loop() {
  if (!running) {
    while (Serial.available()) command(static_cast<char>(Serial.read()));
    if (parked && !Serial) {
      // After capture, stay low power without erasing the result. USB CDC can
      // require a RESET after reconnect; NVS capture survives that reset.
      if (esp_sleep_enable_timer_wakeup(100000) == ESP_OK) esp_light_sleep_start();
      else delay(100);
    } else delay(10);
    return;
  }
  // No console printing or flash writes in the measured interval.
  serviceReader();
  uint32_t now = millis();
  if (!sampled || uint32_t(now - lastSample) >= Probe::SAMPLE_MS) {
    if (sampled && uint32_t(now - lastSample) > capture.maxGap) capture.maxGap = uint32_t(now - lastSample);
    lastSample = now; sampled = true;
    const uint16_t mm = tof.readRangeSingleMillimeters();
    const bool valid = !tof.timeoutOccurred() && mm > 0 && mm <= 2000;
    ++capture.samplesSeen; if (!valid) ++capture.invalidSamples;
    // Use completion time, not a timestamp taken before a blocking I2C read.
    now = millis();
    if (!triggered && valid && mm < Probe::THRESHOLD_MM) {
      triggered = true; capture.trigger = now;
      if (havePreviousSample) capture.samples[capture.sampleCount++] = previousSample;
    }
    if (triggered) {
      if (capture.sampleCount < SAMPLE_CAP) capture.samples[capture.sampleCount++] =
        Sample{now, mm, static_cast<uint16_t>((valid ? 1 : 0) | (valid && mm < 200 ? 2 : 0) | (powered ? 4 : 0))};
      else ++capture.sampleDrops;
    }
    previousSample = Sample{now, mm, static_cast<uint16_t>((valid ? 1 : 0) | (valid && mm < 200 ? 2 : 0) | (powered ? 4 : 0))};
    havePreviousSample = true;
    control.tick(now); syncScan(); // Expose timeout/OFF before an edge starts another window.
    control.sample(now, valid, mm); syncScan();
  }
  serviceReader();
  now = millis();
  if ((triggered && uint32_t(now - capture.trigger) >= Probe::CAPTURE_MS) ||
      (!triggered && uint32_t(now - capture.started) >= Probe::WAIT_MS)) { finishRun(); return; }
  const uint32_t sleepMs = Probe::sleepBudget(now, lastSample, control.sleepAllowed() && reader.available() == 0);
  if (sleepMs) {
    const int64_t before = esp_timer_get_time();
    const esp_err_t armed = esp_sleep_enable_timer_wakeup(uint64_t(sleepMs) * 1000);
    const esp_err_t result = armed == ESP_OK ? esp_light_sleep_start() : armed;
    if (result == ESP_OK) { ++capture.sleepCalls; capture.sleepMs += uint32_t((esp_timer_get_time() - before) / 1000); }
    else { ++capture.sleepErrors; if (capture.sleepErrors == 1) event(SleepError, 0, static_cast<uint32_t>(result)); delay(1); }
  } else delay(1);
}
