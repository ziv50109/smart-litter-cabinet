#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <Update.h>
#include <VL53L0X.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <atomic>
#include <memory>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <esp_pm.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_app_desc.h>
#include <esp_arduino_version.h>
#include "runtime_policy.h"
#include "trace_buffer.h"
#include "app_config.h"
#include "visit_logic.h"
#include "certificates.h"
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 3, 11)
#error "Use Arduino-ESP32 3.3.11 or newer for the management authentication implementation."
#endif

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef DEBUG_WEB_SERVER
#define DEBUG_WEB_SERVER false
#endif

#ifndef WEB_ADMIN_USER
#define WEB_ADMIN_USER ""
#endif
#ifndef WEB_ADMIN_PASSWORD
#define WEB_ADMIN_PASSWORD ""
#endif
#ifndef LITTER_CONNECTED_STANDBY
#define LITTER_CONNECTED_STANDBY 0
#endif

static const char *FW_VERSION = "main-2026-09-17";

struct ScopedLock {
  SemaphoreHandle_t handle;
  bool held;
  explicit ScopedLock(SemaphoreHandle_t h, TickType_t wait = portMAX_DELAY)
    : handle(h), held(h && xSemaphoreTake(h, wait) == pdTRUE) {}
  ~ScopedLock() { if (held) xSemaphoreGive(handle); }
  ScopedLock(const ScopedLock &) = delete;
  ScopedLock &operator=(const ScopedLock &) = delete;
};
SemaphoreHandle_t sensorMutex, queueMutex, diagMutex, logMutex, powerGate;
QueueHandle_t freeDiagnostics, readyDiagnostics;
std::atomic<uint32_t> maintenanceRequest{0};
std::atomic<bool> otaPaused{false};
std::atomic<bool> connectedStandby{false};
std::atomic<bool> automaticPm{false};
std::atomic<const char *> powerReason{"offline_selected"};
String bootId;
uint32_t globalMaxGap = 0, lastCompletedRange = 0, diagnosticsDropped = 0;
#if CONFIG_PM_ENABLE
esp_pm_lock_handle_t rfidSleepLock = nullptr, rfidClockLock = nullptr;
#endif


struct SessionRecord {
  String session_id, chip_id, cat_id, enter_time, exit_time;
  uint32_t duration_sec = 0;
  uint16_t min_distance_mm = 0;
  float avg_distance_mm = 0;
  uint32_t sample_count = 0;
};

struct ScanMetrics {
  uint32_t scans = 0, bytes = 0, recognized = 0, onMs = 0;
  uint32_t firstByteMs = UINT32_MAX, validMs = UINT32_MAX;
  Visit::ScanResult result = Visit::ScanResult::None;
};
struct SessionMetrics {
  ScanMetrics entryScan, exitScan;
  bool open = false;
  uint32_t started = 0;
  uint32_t samples = 0, invalid = 0, maxGap = 0, lastSample = 0;
  uint16_t minMm = UINT16_MAX;
  uint64_t sumMm = 0;
  uint32_t scanCount = 0, recognized = 0, otherValid = 0, badFrames = 0, bytes = 0;
  uint32_t rfidOnMs = 0, firstByteMinMs = UINT32_MAX, validMinMs = UINT32_MAX;
  uint32_t sleepMs = 0, sleepCalls = 0, sleepErrors = 0;
};

using TraceBuffer = Trace::Buffer<Config::TRACE_SAMPLE_CAP, Config::TRACE_EVENT_CAP>;
TraceBuffer trace;
struct DiagnosticSnapshot {
  TraceBuffer trace;
  SessionMetrics metrics;
  String sid, cat;
  Visit::Reason reason = Visit::Reason::None;
  uint32_t durationMs = 0, elapsedMs = 0, maxGlobalGap = 0;
  bool recordQueued = false;
};
DiagnosticSnapshot diagnosticSnapshots[Config::DIAG_SNAPSHOT_SLOTS];

VL53L0X tof;
HardwareSerial rfidSerial(1);
Preferences queuePrefs, diagPrefs;
WebServer web(80);
Visit::Engine visit;
SessionMetrics metrics;

bool tofReady = false;
bool rfidPower = false;
bool rfidReceiving = false;
bool rfidFirstByte = false;
bool spiffsReady = false;
uint32_t rfidPowerAt = 0, seenScanGeneration = 0;
String rfidFrame;
uint16_t latestDistance = UINT16_MAX;
uint32_t lastRangeMs = 0;
uint32_t invalidDistanceSinceMs = 0;
uint32_t lastTofInitAttemptMs = 0;

bool webRoutesReady = false, webRunning = false; // Network-task owned.
enum class NetMode { Off, Sta };
std::atomic<NetMode> netMode{NetMode::Off};
Runtime::Window maintenanceWindow;
Runtime::Backoff reconnectBackoff;
bool maintenanceSticky = DEBUG_WEB_SERVER;

struct LiveLog { uint32_t ms; char action[32]; char detail[80]; };
constexpr uint8_t LIVE_LOG_CAP = 64;
LiveLog liveLogs[LIVE_LOG_CAP];
uint32_t liveSeq = 0;

void logLive(const char *action, const String &detail = "") {
  ScopedLock guard(logMutex);
  LiveLog &r = liveLogs[liveSeq++ % LIVE_LOG_CAP];
  r.ms = millis();
  strlcpy(r.action, action, sizeof(r.action));
  strlcpy(r.detail, detail.c_str(), sizeof(r.detail));
}

String jsonEscape(const String &s) {
  String out; out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '\\' || c == '"') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if ((uint8_t)c >= 0x20) out += c;
  }
  return out;
}

String catForChip(const String &chip) {
  if (CAT_1_CHIP_RAW[0] && chip == CAT_1_CHIP_RAW) return CAT_1_NAME;
  if (CAT_2_CHIP_RAW[0] && chip == CAT_2_CHIP_RAW) return CAT_2_NAME;
  return "";
}
bool registeredChip(const String &chip) { return catForChip(chip).length() > 0; }

void traceReset() { trace.reset(); }
void traceEvent(uint8_t code, uint8_t value = 0, uint16_t aux = 0) {
  if (metrics.open) trace.event({millis(), code, value, aux}, code == 10);
}
void traceSample(uint32_t ms, uint16_t mm, bool valid, bool blocked) {
  if (!metrics.open) return;
  trace.add({ms, mm, uint8_t((valid ? 1 : 0) | (blocked ? 2 : 0) | (rfidPower ? 4 : 0))});
  const uint8_t state = !valid ? 0 : blocked ? 2 : 1;
  if (state != trace.lastDistanceState) {
    trace.event({ms, 7, state, 0});
    trace.lastDistanceState = state;
  }
}
ScanMetrics &currentScanMetrics() { return visit.exitScan ? metrics.exitScan : metrics.entryScan; }

void rfidSet(bool on) {
  if (rfidPower == on) return;
  uint32_t now = millis();
#if CONFIG_PM_ENABLE
  if (on && automaticPm.load()) {
    ESP_ERROR_CHECK(esp_pm_lock_acquire(rfidSleepLock));
    ESP_ERROR_CHECK(esp_pm_lock_acquire(rfidClockLock));
  }
#endif
  digitalWrite(Config::RFID_ENABLE_PIN, on ? HIGH : LOW);
  if (on) { rfidPowerAt = now; traceEvent(1); }
  else {
    if (metrics.open) {
      metrics.rfidOnMs += uint32_t(now - rfidPowerAt);
      currentScanMetrics().onMs += uint32_t(now - rfidPowerAt);
      currentScanMetrics().result = visit.scanResult;
    }
    traceEvent(2);
  }
  rfidPower = on;
#if CONFIG_PM_ENABLE
  if (!on && automaticPm.load()) {
    ESP_ERROR_CHECK(esp_pm_lock_release(rfidClockLock));
    ESP_ERROR_CHECK(esp_pm_lock_release(rfidSleepLock));
  }
#endif
  logLive(on ? "rfid_on" : "rfid_off", String("scan=") + visit.scanGeneration);
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
bool parseRfid(const String &frame, String &chip) {
  if (frame.length() != 18 || frame[0] != 'F') return false;
  uint8_t sum = (uint8_t)frame[0];
  for (uint8_t i = 1; i <= 15; ++i) {
    if (!isDigit(frame[i])) return false;
    sum ^= (uint8_t)frame[i];
  }
  int hi = hexNibble(frame[16]), lo = hexNibble(frame[17]);
  if (hi < 0 || lo < 0 || sum != (uint8_t)((hi << 4) | lo)) return false;
  chip = frame.substring(1, 16);
  return true;
}

void syncRfidPower() {
  if (visit.scanGeneration != seenScanGeneration) {
    seenScanGeneration = visit.scanGeneration;
    rfidFirstByte = false;
    rfidReceiving = false;
    rfidFrame = "";
    while (rfidSerial.available()) rfidSerial.read();
    if (metrics.open) { ++metrics.scanCount; ++currentScanMetrics().scans; }
    traceEvent(3, visit.exitScan ? 1 : 0);
    logLive("scan_start", visit.exitScan ? "exit" : "entry");
  }
  rfidSet(visit.scanning);
}

void pollRfid() {
  const Visit::Phase before = visit.phase;
  visit.tick(millis());
  if (visit.phase != before) traceEvent(8, uint8_t(visit.phase));
  syncRfidPower();
  for (unsigned budget = 0; budget < 96 && rfidSerial.available(); ++budget) {
    char c = (char)rfidSerial.read();
    if (!visit.scanning) continue;
    if (metrics.open) { ++metrics.bytes; ++currentScanMetrics().bytes; }
    if (!rfidFirstByte) {
      rfidFirstByte = true;
      uint32_t latency = uint32_t(millis() - visit.scanStarted);
      if (metrics.open) {
        metrics.firstByteMinMs = min(metrics.firstByteMinMs, latency);
        currentScanMetrics().firstByteMs = min(currentScanMetrics().firstByteMs, latency);
      }
      traceEvent(4, 0, (uint16_t)min(latency, 65535UL));
      logLive("rfid_byte", String(latency) + "ms");
    }
    if (c == '$') { rfidReceiving = true; rfidFrame = ""; continue; }
    if (c == '#' && rfidReceiving) {
      String chip;
      bool valid = parseRfid(rfidFrame, chip);
      rfidReceiving = false;
      if (!valid) { if (metrics.open) ++metrics.badFrames; traceEvent(6); continue; }
      String cat = catForChip(chip);
      uint32_t latency = uint32_t(millis() - visit.scanStarted);
      if (!cat.length()) { if (metrics.open) ++metrics.otherValid; traceEvent(5, 0, (uint16_t)min(latency,65535UL)); continue; }
      const Visit::Phase beforeChip = visit.phase;
      if (visit.acceptChip(millis(), chip.c_str())) {
        if (metrics.open) {
          ++metrics.recognized;
          ++currentScanMetrics().recognized;
          currentScanMetrics().validMs = min(currentScanMetrics().validMs, latency);
          metrics.validMinMs = min(metrics.validMinMs, latency);
        }
        traceEvent(5, chip == CAT_1_CHIP_RAW ? 1 : 2, (uint16_t)min(latency,65535UL));
        logLive("rfid_valid", cat + " " + String(latency) + "ms");
      }
      if (visit.phase != beforeChip) traceEvent(8, uint8_t(visit.phase));
      syncRfidPower();
      continue;
    }
    if (rfidReceiving) {
      if (rfidFrame.length() < 24) rfidFrame += c;
      else { rfidReceiving = false; if (metrics.open) ++metrics.badFrames; traceEvent(6); }
    }
  }
  syncRfidPower();
}

bool initTof() {
  lastTofInitAttemptMs = millis();
  Wire.begin(Config::TOF_SDA_PIN, Config::TOF_SCL_PIN);
  Wire.setClock(400000);
  tof.setTimeout(100);
  tofReady = tof.init() && tof.setMeasurementTimingBudget(Config::TOF_TIMING_BUDGET_US);
  logLive(tofReady ? "tof_ready" : "tof_init_failed");
  return tofReady;
}
uint16_t readDistance() {
  if (!tofReady) return UINT16_MAX;
  uint16_t mm = tof.readRangeSingleMillimeters();
  if (tof.timeoutOccurred() || mm == 0 || mm > 2000) return UINT16_MAX;
  return mm;
}
void handleInvalidDistance(uint32_t now) {
  if (!invalidDistanceSinceMs) invalidDistanceSinceMs = now;
  if (uint32_t(now - invalidDistanceSinceMs) < Config::TOF_INVALID_RESTART_MS) return;
  if (uint32_t(now - lastTofInitAttemptMs) < Config::TOF_REINIT_BACKOFF_MS) return;
  logLive("tof_reinit", "invalid for 5s");
  if (initTof()) invalidDistanceSinceMs = 0;
}

String newSessionId() {
  char b[48];
  snprintf(b, sizeof(b), "%08lX-%08lX-%06llX", (unsigned long)esp_random(),
    (unsigned long)millis(), (unsigned long long)(ESP.getEfuseMac() & 0xFFFFFFULL));
  return String(b);
}

bool timeValid() { return time(nullptr) > 1700000000; }
String formatIso(time_t epoch) {
  struct tm t; gmtime_r(&epoch, &t); char b[25];
  strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &t); return String(b);
}
String epochForUptime(uint32_t targetMs) {
  if (!timeValid()) {
    // Durable same-boot timestamp, resolved after NTP without blocking detection.
    const uint64_t target = uint64_t(esp_timer_get_time() / 1000) - uint32_t(millis() - targetMs);
    char number[24]; snprintf(number, sizeof(number), "%llu", (unsigned long long)target);
    return "@" + bootId + ":" + number;
  }
  time_t now = time(nullptr);
  uint32_t delta = uint32_t(millis() - targetMs) / 1000UL;
  return formatIso(now - delta);
}

void metricsBegin(uint32_t now, uint16_t firstMm) {
  metrics = SessionMetrics{};
  metrics.open = true; metrics.started = now; metrics.lastSample = now;
  if (firstMm != UINT16_MAX) { metrics.samples = 1; metrics.minMm = firstMm; metrics.sumMm = firstMm; }
  traceReset();
  logLive("session_start", String(firstMm) + "mm");
  traceSample(now, firstMm, firstMm != UINT16_MAX, firstMm != UINT16_MAX && firstMm < Config::ENTRY_THRESHOLD_MM);
}
void metricsSample(uint32_t now, uint16_t mm) {
  if (!metrics.open) return;
  if (metrics.lastSample) metrics.maxGap = max(metrics.maxGap, uint32_t(now - metrics.lastSample));
  metrics.lastSample = now;
  if (mm == UINT16_MAX) ++metrics.invalid;
  else { ++metrics.samples; metrics.minMm = min(metrics.minMm, mm); metrics.sumMm += mm; }
  traceSample(now, mm, mm != UINT16_MAX, mm != UINT16_MAX && mm < Config::ENTRY_THRESHOLD_MM);
}

uint32_t fnv1a32(const String &text) {
  uint32_t hash = 0x811C9DC5;
  for (size_t i = 0; i < text.length(); ++i) {
    hash ^= static_cast<uint8_t>(text[i]);
    hash *= 0x01000193;
  }
  return hash;
}
bool parseDecimal(const String &value, uint32_t &result) {
  if (!value.length()) return false;
  uint32_t parsed = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    if (!isDigit(value[i])) return false;
    const uint8_t digit = value[i] - '0';
    if (parsed > (UINT32_MAX - digit) / 10) return false;
    parsed = parsed * 10 + digit;
  }
  result = parsed;
  return true;
}
bool parseFiniteDecimalFloat(const String &value, float &result) {
  if (!value.length()) return false;
  bool sawDot = false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (isDigit(c)) continue;
    if (c == '.' && !sawDot && i > 0 && i + 1 < value.length()) { sawDot = true; continue; }
    return false;
  }
  char *end = nullptr;
  const float parsed = strtof(value.c_str(), &end);
  if (end != value.c_str() + value.length() || !isfinite(parsed)) return false;
  result = parsed;
  return true;
}
bool parseHex32(const String &value, uint32_t &result) {
  if (!value.length() || value.length() > 8) return false;
  uint32_t parsed = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    const int nibble = hexNibble(value[i]);
    if (nibble < 0) return false;
    parsed = (parsed << 4) | static_cast<uint32_t>(nibble);
  }
  result = parsed;
  return true;
}

String queueKey(uint8_t i) { return "q" + String(i); }
String queueMetaPayload(uint8_t head, uint8_t count) { return "1:" + String(head) + ":" + String(count); }
bool storeQueueMeta(uint8_t head, uint8_t count) {
  if (head >= Config::MAX_PENDING_RECORDS || count > Config::MAX_PENDING_RECORDS) return false;
  const String payload = queueMetaPayload(head, count);
  const String encoded = payload + ":" + String(fnv1a32(payload), HEX);
  return queuePrefs.putString("qmeta", encoded) == encoded.length();
}
bool loadQueueMeta(uint8_t &head, uint8_t &count) {
  const String encoded = queuePrefs.getString("qmeta", "");
  if (!encoded.length()) {
    const uint8_t legacyCount = min(queuePrefs.getUChar("qcount", 0), Config::MAX_PENDING_RECORDS);
    head = 0;
    count = legacyCount;
    return storeQueueMeta(head, count);
  }
  String fields[4]; uint8_t field = 0;
  for (size_t i = 0; i <= encoded.length(); ++i) {
    if (i == encoded.length() || encoded[i] == ':') { if (++field > 4) break; }
    else if (field < 4) fields[field] += encoded[i];
  }
  uint32_t parsedHead = 0, parsedCount = 0, storedChecksum = 0;
  const bool valid = field == 4 && fields[0] == "1" &&
    parseDecimal(fields[1], parsedHead) && parseDecimal(fields[2], parsedCount) &&
    parseHex32(fields[3], storedChecksum) && parsedHead < Config::MAX_PENDING_RECORDS &&
    parsedCount <= Config::MAX_PENDING_RECORDS &&
    storedChecksum == fnv1a32("1:" + fields[1] + ":" + fields[2]);
  if (!valid) { head = count = 0; return false; }
  head = static_cast<uint8_t>(parsedHead);
  count = static_cast<uint8_t>(parsedCount);
  return true;
}
uint8_t pendingCount() {
  ScopedLock guard(queueMutex);
  uint8_t head = 0, count = 0;
  return loadQueueMeta(head, count) ? count : 0;
}
String encodeRecord(const SessionRecord &r) {
  const char s = 0x1F;
  return r.session_id+s+r.chip_id+s+r.cat_id+s+r.enter_time+s+r.exit_time+s+String(r.duration_sec)+s+
    String(r.min_distance_mm)+s+String(r.avg_distance_mm,1)+s+String(r.sample_count);
}
bool decodeRecord(const String &encoded, SessionRecord &r) {
  String f[9]; uint8_t n = 0;
  for (size_t i = 0; i <= encoded.length(); ++i) {
    if (i == encoded.length() || encoded[i] == 0x1F) { if (++n > 9) return false; }
    else { if (n >= 9) return false; f[n] += encoded[i]; }
  }
  if (n != 9) return false;
  uint32_t duration = 0, minMm = 0, samples = 0; float avgMm = 0;
  if (!parseDecimal(f[5], duration) || !parseDecimal(f[6], minMm) ||
      !parseFiniteDecimalFloat(f[7], avgMm) || !parseDecimal(f[8], samples) ||
      !f[0].length() || f[0].length() > 80 || f[1].length() > 64 || f[2].length() > 64 ||
      duration < 1 || duration > 86400 || minMm < 1 || minMm > 2000 ||
      avgMm < 1 || avgMm > 2000 || samples < 1 || samples > 100000) return false;
  r.session_id=f[0]; r.chip_id=f[1]; r.cat_id=f[2]; r.enter_time=f[3]; r.exit_time=f[4];
  r.duration_sec=duration; r.min_distance_mm=(uint16_t)minMm; r.avg_distance_mm=avgMm; r.sample_count=samples;
  return true;
}
bool enqueueRecord(const SessionRecord &r) {
  ScopedLock guard(queueMutex);
  uint8_t head = 0, count = 0;
  if (!loadQueueMeta(head, count) || count >= Config::MAX_PENDING_RECORDS) return false;
  const uint8_t tail = (head + count) % Config::MAX_PENDING_RECORDS;
  const String encoded = encodeRecord(r);
  if (queuePrefs.putString(queueKey(tail).c_str(), encoded) != encoded.length()) return false;
  // Publish the new record only after its payload is durable.
  return storeQueueMeta(head, count + 1);
}
bool popRecord() {
  uint8_t head = 0, count = 0;
  if (!loadQueueMeta(head, count)) return false;
  if (!count) return true;
  // Advance one metadata value; stale payload slots are intentionally left in place.
  return storeQueueMeta((head + 1) % Config::MAX_PENDING_RECORDS, count - 1);
}

bool connectSta(uint32_t timeoutMs) {
  if (!strlen(WIFI_SSID)) return false;
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false); // Retry policy is bounded and owned by networkTask.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && uint32_t(millis() - started) < timeoutMs) delay(50);
  if (WiFi.status() == WL_CONNECTED) {
    netMode.store(NetMode::Sta);
    if (connectedStandby.load() && !WiFi.setSleep(WIFI_PS_MIN_MODEM)) {
      connectedStandby.store(false);
      powerReason.store("wifi_power_save_unavailable");
      logLive("power_fallback", powerReason.load());
    }
    return true;
  }
  WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF); netMode.store(NetMode::Off);
  return false;
}
void syncClock() {
  if (timeValid() || WiFi.status() != WL_CONNECTED) return;
  configTzTime("UTC0", "time.google.com", "pool.ntp.org");
  uint32_t started = millis();
  while (!timeValid() && uint32_t(millis() - started) < Config::NTP_TIMEOUT_MS) delay(50);
}

bool resolveTimestamp(String &stamp) {
  if (!stamp.startsWith("@")) return stamp.length() > 0;
  const String prefix = "@" + bootId + ":";
  if (!timeValid() || !stamp.startsWith(prefix)) return false;
  const String number = stamp.substring(prefix.length());
  if (!number.length() || number.length() > 20) return false;
  uint64_t ms = 0;
  for (size_t i = 0; i < number.length(); ++i) {
    if (number[i] < '0' || number[i] > '9') return false;
    const uint8_t d = number[i] - '0';
    if (ms > (UINT64_MAX - d) / 10) return false;
    ms = ms * 10 + d;
  }
  const uint64_t now = uint64_t(esp_timer_get_time() / 1000);
  if (ms > now) return false;
  stamp = formatIso(time(nullptr) - (now - ms) / 1000);
  return true;
}

String recordJson(const SessionRecord &r) {
  String j; j.reserve(700);
  j="{\"device_token\":\""+jsonEscape(String(DEVICE_TOKEN))+"\",\"session\":{";
  j+="\"session_id\":\""+jsonEscape(r.session_id)+"\",";
  j+="\"chip_id\":\""+jsonEscape(r.chip_id)+"\",";
  j+="\"cat_id\":\""+jsonEscape(r.cat_id)+"\",";
  j+="\"enter_time\":\""+jsonEscape(r.enter_time)+"\",";
  j+="\"exit_time\":\""+jsonEscape(r.exit_time)+"\",";
  j+="\"duration_sec\":"+String(r.duration_sec)+",\"min_distance_mm\":"+String(r.min_distance_mm)+",";
  j+="\"avg_distance_mm\":"+String(r.avg_distance_mm,1)+",\"sample_count\":"+String(r.sample_count)+"}}";
  return j;
}

bool uploadRecord(const SessionRecord &r) {
  if (WiFi.status()!=WL_CONNECTED || !strlen(APP_SCRIPT_URL) || !strlen(DEVICE_TOKEN)) return false;
  WiFiClientSecure client; client.setCACert(GOOGLE_ROOT_CA_BUNDLE);
  HTTPClient http; http.setConnectTimeout(10000); http.setTimeout(15000); http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client,APP_SCRIPT_URL)) return false;
  http.addHeader("Content-Type","application/json");
  int status=http.POST(recordJson(r)); String response=status>0?http.getString():""; String location=http.getLocation(); http.end();
  if (status==HTTP_CODE_FOUND || status==HTTP_CODE_SEE_OTHER) {
    if (!location.startsWith("https://")) return false;
    WiFiClientSecure c2; c2.setCACert(GOOGLE_ROOT_CA_BUNDLE);
    HTTPClient h2; h2.setConnectTimeout(10000); h2.setTimeout(15000); h2.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!h2.begin(c2,location)) return false;
    status=h2.GET(); response=status>0?h2.getString():""; h2.end();
  }
  return status>=200 && status<300 && response.indexOf("\"ok\":true")>=0;
}

void uploadPending() {
  // Copy under the queue lock, but NEVER hold it across Wi-Fi/NTP/HTTP waits.
  SessionRecord record;
  uint8_t head = 0, count = 0;
  {
    ScopedLock guard(queueMutex);
    if (!loadQueueMeta(head, count) || !count) return;
    if (!decodeRecord(queuePrefs.getString(queueKey(head).c_str(), ""), record)) {
      logLive("queue", "decode/meta failure"); return;
    }
  }
  syncClock();
  const bool changedTime = record.enter_time.startsWith("@") || record.exit_time.startsWith("@");
  if (!resolveTimestamp(record.enter_time) || !resolveTimestamp(record.exit_time)) {
    logLive("queue", "timestamp unavailable; record retained"); return;
  }
  if (changedTime) {
    ScopedLock guard(queueMutex);
    const String encoded = encodeRecord(record);
    if (queuePrefs.putString(queueKey(head).c_str(), encoded) != encoded.length()) return;
  }
  if (!uploadRecord(record)) { logLive("upload", "failed"); return; }
  {
    ScopedLock guard(queueMutex);
    uint8_t currentHead = 0, currentCount = 0;
    if (!loadQueueMeta(currentHead, currentCount) || !currentCount || currentHead != head || !popRecord()) {
      logLive("queue", "ack failure"); return;
    }
  }
  logLive("upload", "ok");
}

void rotateDiagIfNeeded() {
  if (!spiffsReady) return;
  File f=SPIFFS.open("/diag.jsonl",FILE_READ); size_t size=f?f.size():0; if(f)f.close();
  if (size < Config::DIAG_ROTATE_BYTES) return;
  SPIFFS.remove("/diag.prev.jsonl"); SPIFFS.rename("/diag.jsonl","/diag.prev.jsonl");
}
void appendDiag(const String &line) {
  if (!spiffsReady) {
    return;
  }
  rotateDiagIfNeeded();
  File f = SPIFFS.open("/diag.jsonl", FILE_APPEND);
  if (!f) {
    return;
  }
  f.println(line);
  f.close();
}
#include "management_runtime.h"

void finishSession() {
  rfidSet(false);
  traceEvent(10, uint8_t(visit.reason));
  const String chip(visit.chip), cat = catForChip(chip), sid = newSessionId();
  const uint32_t duration = visit.durationMs();
  const bool trustworthy = visit.reason == Visit::Reason::Normal && duration > 0 && registeredChip(chip) && !visit.conflict;
  bool queued = false;
  if (trustworthy) {
    SessionRecord record;
    record.session_id = sid; record.chip_id = chip; record.cat_id = cat;
    record.duration_sec = max(1UL, duration / 1000UL);
    record.min_distance_mm = metrics.minMm == UINT16_MAX ? 0 : metrics.minMm;
    record.avg_distance_mm = metrics.samples ? float(metrics.sumMm) / metrics.samples : 0;
    record.sample_count = metrics.samples;
    record.enter_time = epochForUptime(visit.started); record.exit_time = epochForUptime(visit.exitStarted);
    queued = enqueueRecord(record); // Local durable handoff, no network waits.
    if (!queued) logLive("queue", "full/write failure");
  }
  uint8_t slot = 0;
  if (xQueueReceive(freeDiagnostics, &slot, 0) == pdTRUE) {
    DiagnosticSnapshot &d = diagnosticSnapshots[slot];
    d.trace = trace; d.metrics = metrics; d.sid = sid; d.cat = cat;
    d.reason = visit.reason; d.durationMs = duration;
    d.elapsedMs = uint32_t(visit.finished - metrics.started);
    d.maxGlobalGap = globalMaxGap; d.recordQueued = queued;
    xQueueSend(readyDiagnostics, &slot, 0);
  } else {
    ++diagnosticsDropped;
    logLive("diagnostic_overflow", sid); // Visit record is already durable; never pretend trace was saved.
  }
  logLive("session_end", String(Visit::reasonName(visit.reason)) + " " + String(duration) + "ms");
  visit.release(); metrics.open = false; traceReset();
  maintenanceRequest.store(Config::POST_EVENT_MAINTENANCE_MS);
}

void processSensor() {
  uint32_t now=millis();
  uint32_t period=(visit.phase==Visit::Phase::Idle||visit.phase==Visit::Phase::WaitClear)?Config::IDLE_RANGING_PERIOD_MS:Config::ACTIVE_RANGING_PERIOD_MS;
  if(lastRangeMs && uint32_t(now-lastRangeMs)<period)return;
  lastRangeMs=now; uint16_t mm=readDistance(); latestDistance=mm; now=millis();
  if (lastCompletedRange) globalMaxGap = max(globalMaxGap, uint32_t(now - lastCompletedRange));
  lastCompletedRange = now;
  Visit::Phase before=visit.phase;
  const bool armedBefore = visit.exitArmed;
  visit.sample(now,mm!=UINT16_MAX,mm);
  if(before==Visit::Phase::Idle && visit.phase==Visit::Phase::Candidate) metricsBegin(now,mm);
  else if(metrics.open) metricsSample(now,mm);
  if (visit.phase != before) traceEvent(8, uint8_t(visit.phase));
  if (visit.exitArmed != armedBefore) traceEvent(9, visit.exitArmed ? 1 : 0);
  if(mm==UINT16_MAX) handleInvalidDistance(now); else invalidDistanceSinceMs=0;
  syncRfidPower();
  if(visit.phase==Visit::Phase::Complete)finishSession();
}

#include "power_runtime.h"

void setup() {
  Serial.begin(115200);
  pinMode(Config::RFID_ENABLE_PIN, OUTPUT); digitalWrite(Config::RFID_ENABLE_PIN, LOW);
  pinMode(Config::TOF_INT_PIN, INPUT_PULLUP);
  sensorMutex = xSemaphoreCreateMutex(); queueMutex = xSemaphoreCreateMutex();
  diagMutex = xSemaphoreCreateMutex(); logMutex = xSemaphoreCreateMutex(); powerGate = xSemaphoreCreateMutex();
  freeDiagnostics = xQueueCreate(Config::DIAG_SNAPSHOT_SLOTS, sizeof(uint8_t));
  readyDiagnostics = xQueueCreate(Config::DIAG_SNAPSHOT_SLOTS, sizeof(uint8_t));
  configASSERT(sensorMutex && queueMutex && diagMutex && logMutex && powerGate && freeDiagnostics && readyDiagnostics);
  for (uint8_t i = 0; i < Config::DIAG_SNAPSHOT_SLOTS; ++i) xQueueSend(freeDiagnostics, &i, 0);
  bootId = String(esp_random(), HEX) + String(esp_random(), HEX);
  rfidSerial.setRxBufferSize(512); rfidSerial.begin(9600, SERIAL_8N1, Config::RFID_RX_PIN, -1);
  queuePrefs.begin("litter", false); diagPrefs.begin("litter-diag", false);
  spiffsReady = SPIFFS.begin(true);
  initTof();
  WiFi.persistent(false); WiFi.mode(WIFI_OFF);
  configurePower();
  Serial.print("Power initialization: "); Serial.println(powerReason.load());
  maintenanceRequest.store(Config::BOOT_MAINTENANCE_MS);
  if (xTaskCreate(networkTask, "litter-net", 12288, nullptr, 1, nullptr) != pdPASS) {
    connectedStandby.store(false);
    powerReason.store("network_task_init_failed");
    logLive("network_disabled", "task allocation failed");
  }
  logLive("ready", FW_VERSION);
}

void loop() {
  uint32_t wait = 1;
  {
    ScopedLock guard(sensorMutex);
    static bool wasPaused = false;
    if (otaPaused.load()) { wasPaused = true; wait = Config::NETWORK_POLL_MS; }
    else {
      if (wasPaused) {
        visit = Visit::Engine{}; visit.phase = Visit::Phase::WaitClear;
        seenScanGeneration = 0; lastRangeMs = lastCompletedRange = 0;
        wasPaused = false;
        logLive("detection", "resumed after OTA; wait for clear");
      }
      pollRfid(); processSensor(); pollRfid();
      maybeSleep();
      const uint32_t period = (visit.phase == Visit::Phase::Idle || visit.phase == Visit::Phase::WaitClear)
        ? Config::IDLE_RANGING_PERIOD_MS : Config::ACTIVE_RANGING_PERIOD_MS;
      const uint32_t elapsed = uint32_t(millis() - lastRangeMs);
      wait = rfidPower ? 2 : elapsed < period ? period - elapsed : 1;
    }
  }
  // Real blocking deadlines permit tickless idle; do not busy-poll a sleeping network.
  delay(wait);
}
