#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <VL53L0X.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include "probe_logic.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

constexpr uint8_t ENABLE_PIN = 1;
constexpr uint8_t SDA_PIN = 5;
constexpr uint8_t SCL_PIN = 6;
constexpr uint8_t RX_PIN = 44;
constexpr uint16_t SAMPLE_CAP = 640;
constexpr uint16_t EVENT_CAP = 64;

enum EventCode : uint16_t {
  PowerOn = 1,
  PowerOff,
  ScanStart,
  FirstByte,
  ValidChip,
  BadFrame,
  ScanEnd,
  SensorError,
  StaleBytes,
  SleepError
};

struct Sample {
  uint32_t ms;
  uint16_t mm;
  uint16_t flags;
};

struct Event {
  uint32_t ms;
  uint16_t code;
  uint16_t value;
  uint32_t aux;
};

struct Capture {
  uint32_t started = 0;
  uint32_t ended = 0;
  uint32_t trigger = 0;
  uint32_t samplesSeen = 0;
  uint32_t invalidSamples = 0;
  uint32_t maxGap = 0;
  uint32_t scans = 0;
  uint32_t recognized = 0;
  uint32_t validOther = 0;
  uint32_t bytes = 0;
  uint32_t badFrames = 0;
  uint32_t staleBytes = 0;
  uint32_t onMs = 0;
  uint32_t sleepMs = 0;
  uint32_t sleepCalls = 0;
  uint32_t sleepErrors = 0;
  uint32_t sampleDrops = 0;
  uint32_t eventDrops = 0;
  uint32_t sensorFailed = 0;
  uint16_t sampleCount = 0;
  uint16_t eventCount = 0;
  Sample samples[SAMPLE_CAP];
  Event events[EVENT_CAP];
};

VL53L0X tof;
HardwareSerial reader(1);
WebServer web(80);
Probe::Controller control;
Probe::Parser parser;
Capture capture;

bool running = false;
bool powered = false;
bool triggered = false;
bool sampled = false;
bool gotByte = false;
bool tofReady = false;
bool portalStarted = false;
bool routesRegistered = false;
bool startRequested = false;
bool havePreviousSample = false;
bool otaStarted = false;
bool otaFailed = false;
uint32_t lastSample = 0;
uint32_t poweredAt = 0;
uint32_t seenGeneration = 0;
Sample previousSample{};
Probe::End seenEnd = Probe::End::None;

void startRun();
void startPortal();

void addEvent(uint16_t code, uint16_t value = 0, uint32_t aux = 0) {
  if (capture.eventCount >= EVENT_CAP) {
    ++capture.eventDrops;
    return;
  }
  capture.events[capture.eventCount++] = Event{millis(), code, value, aux};
}

void setReaderPower(bool on) {
  if (powered == on) return;
  const uint32_t now = millis();
  digitalWrite(ENABLE_PIN, on ? HIGH : LOW);
  if (on) poweredAt = now;
  else capture.onMs += uint32_t(now - poweredAt);
  powered = on;
  addEvent(on ? PowerOn : PowerOff, digitalRead(ENABLE_PIN), control.generation);
}

void syncScan() {
  if (control.generation != seenGeneration) {
    seenGeneration = control.generation;
    seenEnd = Probe::End::None;
    parser.reset();
    gotByte = false;

    const int backlog = reader.available();
    for (int i = 0; i < backlog; ++i) reader.read();
    capture.staleBytes += backlog;
    if (backlog) addEvent(StaleBytes, static_cast<uint16_t>(backlog));

    ++capture.scans;
    addEvent(ScanStart, 0, seenGeneration);
  }

  setReaderPower(control.powerWanted());

  if (!control.scanning && control.end != seenEnd) {
    seenEnd = control.end;
    addEvent(ScanEnd, static_cast<uint16_t>(seenEnd), uint32_t(millis() - control.scanStart));
    parser.reset();
  }
}

void serviceReader() {
  control.tick(millis());
  syncScan();

  for (unsigned i = 0; i < 96 && reader.available(); ++i) {
    const char c = static_cast<char>(reader.read());
    control.tick(millis());
    syncScan();

    if (!control.scanning) {
      ++capture.staleBytes;
      continue;
    }

    ++capture.bytes;
    if (!gotByte) {
      gotByte = true;
      addEvent(FirstByte, 0, uint32_t(millis() - control.scanStart));
    }

    char chip[16] = {};
    const Probe::Parse result = parser.feed(c, chip);
    if (result == Probe::Parse::Invalid) {
      ++capture.badFrames;
      addEvent(BadFrame);
      continue;
    }
    if (result != Probe::Parse::Valid) continue;

    const uint16_t cat = CAT_1_CHIP_RAW[0] && strcmp(chip, CAT_1_CHIP_RAW) == 0 ? 1 :
                         CAT_2_CHIP_RAW[0] && strcmp(chip, CAT_2_CHIP_RAW) == 0 ? 2 : 0;
    addEvent(ValidChip, cat, uint32_t(millis() - control.scanStart));

    if (!cat) {
      ++capture.validOther;
      continue;
    }

    if (control.accept(millis())) ++capture.recognized;
    syncScan();
  }
}

uint16_t lastRecognizedCat() {
  for (int i = int(capture.eventCount) - 1; i >= 0; --i) {
    const Event &e = capture.events[i];
    if (e.code == ValidChip && (e.value == 1 || e.value == 2)) return e.value;
  }
  return 0;
}

const char *catLabel(uint16_t cat) {
  if (cat == 1) return CAT_1_NAME;
  if (cat == 2) return CAT_2_NAME;
  return "未辨識";
}

String pageHtml() {
  const bool hasResult = capture.ended != 0;
  const uint16_t cat = lastRecognizedCat();
  String html;
  html.reserve(7000);
  html += F("<!doctype html><html lang='zh-Hant'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<style>body{font-family:system-ui;margin:20px;line-height:1.55}table{border-collapse:collapse;width:100%;max-width:760px}td,th{border:1px solid #bbb;padding:7px;text-align:left}button,input,a{font-size:16px;padding:10px 14px;margin:8px 4px 8px 0}.box{border:1px solid #bbb;padding:14px;max-width:760px;margin-top:18px}</style>");
  html += F("<h1>LitterProbe</h1>");
  html += F("<div class='box'><h2>低功耗 / RFID 驗證</h2>");
  if (!hasResult) {
    html += F("<p>目前是維護模式：RFID 關閉，尚未開始測試。按下開始後 Wi-Fi 會消失；測試期間 Wi-Fi 完全關閉。</p>");
  } else {
    html += F("<p><b>上一輪測試期間 Wi-Fi 是關閉的。</b>目前 Wi-Fi 是測試完成後重新開啟。</p>");
  }
  html += F("<form method='post' action='/start'><button type='submit'>開始低功耗測試</button></form>");
  html += F("<p>開始後最長等待 15 分鐘；第一次有效 &lt;200mm 後記錄 60 秒，完成後 LitterProbe Wi-Fi 會自動重新出現。</p></div>");

  if (hasResult) {
    html += F("<h2>上一輪測試結果</h2><table><tr><th>項目</th><th>結果</th></tr>");
    html += F("<tr><td>入口是否觸發 &lt;200mm</td><td>"); html += capture.trigger ? "是" : "否（15 分鐘內沒有觸發）"; html += F("</td></tr>");
    html += F("<tr><td>辨識貓咪</td><td>"); html += catLabel(cat); html += F("</td></tr>");
    html += F("<tr><td>RFID 掃描窗口</td><td>"); html += capture.scans; html += F("</td></tr>");
    html += F("<tr><td>成功辨識窗口</td><td>"); html += capture.recognized; html += F("</td></tr>");
    html += F("<tr><td>RFID GPIO HIGH 累積</td><td>"); html += capture.onMs; html += F(" ms</td></tr>");
    html += F("<tr><td>UART bytes / 壞封包</td><td>"); html += capture.bytes; html += " / "; html += capture.badFrames; html += F("</td></tr>");
    html += F("<tr><td>測距 samples / invalid</td><td>"); html += capture.samplesSeen; html += " / "; html += capture.invalidSamples; html += F("</td></tr>");
    html += F("<tr><td>最大取樣間隔</td><td>"); html += capture.maxGap; html += F(" ms</td></tr>");
    html += F("<tr><td>Light-sleep</td><td>"); html += capture.sleepCalls; html += F(" 次 / "); html += capture.sleepMs; html += F(" ms / errors "); html += capture.sleepErrors; html += F("</td></tr>");
    html += F("<tr><td>Sensor failed</td><td>"); html += capture.sensorFailed; html += F("</td></tr>");
    html += F("<tr><td>Trace drops</td><td>samples "); html += capture.sampleDrops; html += F(" / events "); html += capture.eventDrops; html += F("</td></tr></table>");
    html += F("<p><a href='/raw'>查看完整原始紀錄</a></p>");
  }

  html += F("<div class='box'><h2>無線更新韌體</h2><p>只選 Arduino 匯出的 <code>*.ino.bin</code> application image。更新成功後 ESP32 會自動重新啟動。</p>");
  html += F("<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='firmware' accept='.bin,application/octet-stream' required><button type='submit'>上傳並更新</button></form></div>");
  html += F("</html>");
  return html;
}

String rawText() {
  if (!capture.ended) return "NO_CAPTURE\n";
  String out;
  out.reserve(22000);
  out += "SUMMARY\n";
  out += "started=" + String(capture.started) + ",ended=" + String(capture.ended) + ",trigger=" + String(capture.trigger) + "\n";
  out += "samples=" + String(capture.samplesSeen) + ",invalid=" + String(capture.invalidSamples) + ",max_gap_ms=" + String(capture.maxGap) + "\n";
  out += "scans=" + String(capture.scans) + ",recognized=" + String(capture.recognized) + ",other_valid=" + String(capture.validOther) + "\n";
  out += "commanded_on_ms=" + String(capture.onMs) + ",sleep_ms=" + String(capture.sleepMs) + ",sleep_calls=" + String(capture.sleepCalls) + ",sleep_errors=" + String(capture.sleepErrors) + "\n";
  out += "bytes=" + String(capture.bytes) + ",bad_frames=" + String(capture.badFrames) + ",stale_bytes=" + String(capture.staleBytes) + "\n";
  out += "sample_drops=" + String(capture.sampleDrops) + ",event_drops=" + String(capture.eventDrops) + ",sensor_failed=" + String(capture.sensorFailed) + "\n";
  out += "S,uptime_ms,distance_mm,flags(bit0=valid bit1=blocked bit2=GPIO_on)\n";
  for (uint16_t i = 0; i < capture.sampleCount; ++i) {
    const Sample &s = capture.samples[i];
    out += "S," + String(s.ms) + "," + String(s.mm) + "," + String(s.flags) + "\n";
  }
  out += "E,uptime_ms,code,value,aux\n";
  for (uint16_t i = 0; i < capture.eventCount; ++i) {
    const Event &e = capture.events[i];
    out += "E," + String(e.ms) + "," + String(e.code) + "," + String(e.value) + "," + String(e.aux) + "\n";
  }
  return out;
}

void registerRoutes() {
  if (routesRegistered) return;

  web.on("/", HTTP_GET, []() {
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "text/html; charset=utf-8", pageHtml());
  });
  web.on("/raw", HTTP_GET, []() {
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "text/plain; charset=utf-8", rawText());
  });
  web.on("/start", HTTP_POST, []() {
    if (running) {
      web.send(409, "text/plain; charset=utf-8", "測試已在執行中。");
      return;
    }
    web.send(200, "text/plain; charset=utf-8", "測試開始。Wi-Fi 即將關閉；完成後重新連線 LitterProbe 並開啟 http://192.168.4.1/");
    startRequested = true;
  });
  web.on("/rerun", HTTP_POST, []() {
    web.send(200, "text/plain; charset=utf-8", "測試開始。Wi-Fi 即將關閉；完成後重新連線 LitterProbe 並開啟 http://192.168.4.1/");
    startRequested = true;
  });
  web.on("/update", HTTP_POST,
    []() {
      const bool ok = otaStarted && !otaFailed && !Update.hasError();
      web.sendHeader("Connection", "close");
      web.send(ok ? 200 : 500, "text/plain; charset=utf-8",
        ok ? "更新成功，裝置即將重新啟動。" : "更新失敗，原韌體保持可開機；請返回重試。");
      if (ok) {
        delay(400);
        ESP.restart();
      }
      otaStarted = false;
      otaFailed = false;
    },
    []() {
      HTTPUpload &upload = web.upload();
      if (upload.status == UPLOAD_FILE_START) {
        otaStarted = true;
        otaFailed = false;
        setReaderPower(false);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) otaFailed = true;
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!otaFailed && Update.write(upload.buf, upload.currentSize) != upload.currentSize) otaFailed = true;
      } else if (upload.status == UPLOAD_FILE_END) {
        if (!otaFailed && !Update.end(true)) otaFailed = true;
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        otaFailed = true;
        Update.abort();
      }
    });
  web.onNotFound([]() { web.send(404, "text/plain; charset=utf-8", "Not found"); });
  routesRegistered = true;
}

void startPortal() {
  if (portalStarted) return;
  setReaderPower(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("LitterProbe");
  registerRoutes();
  web.begin();
  portalStarted = true;
}

void stopPortal() {
  if (!portalStarted) return;
  web.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  portalStarted = false;
}

void finishRun(bool sensorFailed = false) {
  control.cancel();
  syncScan();
  if (powered) setReaderPower(false);
  capture.ended = millis();
  if (sensorFailed) capture.sensorFailed = 1;
  running = false;
  reader.end();
  startPortal();
}

void startRun() {
  capture = Capture{};
  control = Probe::Controller{};
  parser.reset();
  running = false;
  powered = false;
  triggered = false;
  sampled = false;
  gotByte = false;
  tofReady = false;
  havePreviousSample = false;
  otaStarted = false;
  otaFailed = false;
  lastSample = 0;
  poweredAt = 0;
  seenGeneration = 0;
  seenEnd = Probe::End::None;

  digitalWrite(ENABLE_PIN, LOW);
  WiFi.mode(WIFI_OFF);
  reader.setRxBufferSize(512);
  reader.begin(9600, SERIAL_8N1, RX_PIN, -1);
  Wire.begin(SDA_PIN, SCL_PIN);
  tof.setTimeout(100);
  tofReady = tof.init() && tof.setMeasurementTimingBudget(20000);
  capture.started = millis();
  lastSample = capture.started;
  running = true;

  if (!tofReady) {
    addEvent(SensorError);
    finishRun(true);
  }
}

void setup() {
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, LOW);
  startPortal();
}

void loop() {
  if (!running) {
    if (portalStarted) {
      web.handleClient();
      if (startRequested) {
        startRequested = false;
        delay(250);
        stopPortal();
        startRun();
      }
    }
    delay(5);
    return;
  }

  serviceReader();
  uint32_t now = millis();

  if (!sampled || uint32_t(now - lastSample) >= Probe::SAMPLE_MS) {
    if (sampled && uint32_t(now - lastSample) > capture.maxGap) {
      capture.maxGap = uint32_t(now - lastSample);
    }
    lastSample = now;
    sampled = true;

    const uint16_t mm = tof.readRangeSingleMillimeters();
    const bool valid = !tof.timeoutOccurred() && mm > 0 && mm <= 2000;
    ++capture.samplesSeen;
    if (!valid) ++capture.invalidSamples;
    now = millis();

    if (!triggered && valid && mm < Probe::THRESHOLD_MM) {
      triggered = true;
      capture.trigger = now;
      if (havePreviousSample && capture.sampleCount < SAMPLE_CAP) {
        capture.samples[capture.sampleCount++] = previousSample;
      }
    }

    const uint16_t flags = static_cast<uint16_t>((valid ? 1 : 0) |
      (valid && mm < Probe::THRESHOLD_MM ? 2 : 0) | (powered ? 4 : 0));

    if (triggered) {
      if (capture.sampleCount < SAMPLE_CAP) capture.samples[capture.sampleCount++] = Sample{now, mm, flags};
      else ++capture.sampleDrops;
    }
    previousSample = Sample{now, mm, flags};
    havePreviousSample = true;

    control.tick(now);
    syncScan();
    control.sample(now, valid, mm);
    syncScan();
  }

  serviceReader();
  now = millis();

  if ((triggered && uint32_t(now - capture.trigger) >= Probe::CAPTURE_MS) ||
      (!triggered && uint32_t(now - capture.started) >= Probe::WAIT_MS)) {
    finishRun();
    return;
  }

  const uint32_t sleepMs = Probe::sleepBudget(now, lastSample,
    control.sleepAllowed() && reader.available() == 0);
  if (sleepMs) {
    const int64_t before = esp_timer_get_time();
    const esp_err_t armed = esp_sleep_enable_timer_wakeup(uint64_t(sleepMs) * 1000);
    const esp_err_t result = armed == ESP_OK ? esp_light_sleep_start() : armed;
    if (result == ESP_OK) {
      ++capture.sleepCalls;
      capture.sleepMs += uint32_t((esp_timer_get_time() - before) / 1000);
    } else {
      ++capture.sleepErrors;
      if (capture.sleepErrors == 1) addEvent(SleepError, 0, static_cast<uint32_t>(result));
      delay(1);
    }
  } else {
    delay(1);
  }
}
