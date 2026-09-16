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
#include "app_config.h"
#include "visit_logic.h"
#include "certificates.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef DEBUG_WEB_SERVER
#define DEBUG_WEB_SERVER false
#endif

static const char *FW_VERSION = "main_v2-2026-09-16";

struct SessionRecord {
  String session_id, chip_id, cat_id, enter_time, exit_time;
  uint32_t duration_sec = 0;
  uint16_t min_distance_mm = 0;
  float avg_distance_mm = 0;
  uint32_t sample_count = 0;
};

struct SessionMetrics {
  bool open = false;
  uint32_t started = 0;
  uint32_t samples = 0, invalid = 0, maxGap = 0, lastSample = 0;
  uint16_t minMm = UINT16_MAX;
  uint64_t sumMm = 0;
  uint32_t scanCount = 0, recognized = 0, otherValid = 0, badFrames = 0, bytes = 0;
  uint32_t rfidOnMs = 0, firstByteMinMs = UINT32_MAX, validMinMs = UINT32_MAX;
  uint32_t sleepMs = 0, sleepCalls = 0, sleepErrors = 0;
};

struct TraceSample { uint32_t ms; uint16_t mm; uint8_t flags; };
struct TraceEvent { uint32_t ms; uint8_t code; uint8_t value; uint16_t aux; };
constexpr uint16_t TRACE_CAP = 512;
constexpr uint8_t TRACE_EVENT_CAP = 48;
TraceSample traceSamples[TRACE_CAP];
TraceEvent traceEvents[TRACE_EVENT_CAP];
uint16_t traceHead = 0, traceCount = 0;
uint8_t traceEventCount = 0;

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

bool webRoutesReady = false, webRunning = false;
enum class NetMode { Off, Sta, Ap };
NetMode netMode = NetMode::Off;
uint32_t maintenanceStarted = 0, maintenanceDuration = 0;
bool maintenanceSticky = false;
bool otaStarted = false, otaFailed = false;

struct LiveLog { uint32_t ms; char action[32]; char detail[80]; };
constexpr uint8_t LIVE_LOG_CAP = 64;
LiveLog liveLogs[LIVE_LOG_CAP];
uint32_t liveSeq = 0;

void logLive(const char *action, const String &detail = "") {
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

void traceReset() { traceHead = traceCount = 0; traceEventCount = 0; }
void traceSample(uint32_t ms, uint16_t mm, bool valid, bool blocked) {
  if (!metrics.open) return;
  TraceSample s{ms, mm, (uint8_t)((valid ? 1 : 0) | (blocked ? 2 : 0) | (rfidPower ? 4 : 0))};
  if (traceCount < TRACE_CAP) traceSamples[(traceHead + traceCount++) % TRACE_CAP] = s;
  else { traceSamples[traceHead] = s; traceHead = (traceHead + 1) % TRACE_CAP; }
}
void traceEvent(uint8_t code, uint8_t value = 0, uint16_t aux = 0) {
  if (!metrics.open || traceEventCount >= TRACE_EVENT_CAP) return;
  traceEvents[traceEventCount++] = TraceEvent{millis(), code, value, aux};
}

void rfidSet(bool on) {
  if (rfidPower == on) return;
  uint32_t now = millis();
  digitalWrite(Config::RFID_ENABLE_PIN, on ? HIGH : LOW);
  if (on) { rfidPowerAt = now; traceEvent(1); }
  else {
    if (metrics.open) metrics.rfidOnMs += uint32_t(now - rfidPowerAt);
    traceEvent(2);
  }
  rfidPower = on;
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
    if (metrics.open) ++metrics.scanCount;
    traceEvent(3, visit.exitScan ? 1 : 0);
    logLive("scan_start", visit.exitScan ? "exit" : "entry");
  }
  rfidSet(visit.scanning);
}

void pollRfid() {
  visit.tick(millis());
  syncRfidPower();
  for (unsigned budget = 0; budget < 96 && rfidSerial.available(); ++budget) {
    char c = (char)rfidSerial.read();
    if (!visit.scanning) continue;
    if (metrics.open) ++metrics.bytes;
    if (!rfidFirstByte) {
      rfidFirstByte = true;
      uint32_t latency = uint32_t(millis() - visit.scanStarted);
      if (metrics.open) metrics.firstByteMinMs = min(metrics.firstByteMinMs, latency);
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
      if (visit.acceptChip(millis(), chip.c_str())) {
        if (metrics.open) {
          ++metrics.recognized;
          metrics.validMinMs = min(metrics.validMinMs, latency);
        }
        traceEvent(5, chip == CAT_1_CHIP_RAW ? 1 : 2, (uint16_t)min(latency,65535UL));
        logLive("rfid_valid", cat + " " + String(latency) + "ms");
      }
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
  Wire.begin(Config::TOF_SDA_PIN, Config::TOF_SCL_PIN);
  Wire.setClock(400000);
  tof.setTimeout(100);
  tofReady = tof.init() && tof.setMeasurementTimingBudget(Config::TOF_TIMING_BUDGET_US);
  return tofReady;
}
uint16_t readDistance() {
  if (!tofReady) return UINT16_MAX;
  uint16_t mm = tof.readRangeSingleMillimeters();
  if (tof.timeoutOccurred() || mm == 0 || mm > 2000) return UINT16_MAX;
  return mm;
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
  if (!timeValid()) return "";
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

String queueKey(uint8_t i) { return "q" + String(i); }
uint8_t pendingCount() { return min(queuePrefs.getUChar("count", 0), Config::MAX_PENDING_RECORDS); }
String encodeRecord(const SessionRecord &r) {
  const char s = 0x1F;
  return r.session_id+s+r.chip_id+s+r.cat_id+s+r.enter_time+s+r.exit_time+s+String(r.duration_sec)+s+
    String(r.min_distance_mm)+s+String(r.avg_distance_mm,1)+s+String(r.sample_count);
}
bool decodeRecord(const String &encoded, SessionRecord &r) {
  String f[9]; uint8_t n=0;
  for (size_t i=0;i<=encoded.length();++i) {
    if (i==encoded.length() || encoded[i]==0x1F) { if (++n>9) return false; }
    else if (n<9) f[n]+=encoded[i];
  }
  if (n!=9) return false;
  r.session_id=f[0]; r.chip_id=f[1]; r.cat_id=f[2]; r.enter_time=f[3]; r.exit_time=f[4];
  r.duration_sec=(uint32_t)f[5].toInt(); r.min_distance_mm=(uint16_t)f[6].toInt();
  r.avg_distance_mm=f[7].toFloat(); r.sample_count=(uint32_t)f[8].toInt();
  return r.session_id.length() && r.duration_sec>0 && r.sample_count>0;
}
bool enqueueRecord(const SessionRecord &r) {
  uint8_t count = pendingCount(); if (count >= Config::MAX_PENDING_RECORDS) return false;
  String e=encodeRecord(r); if (queuePrefs.putString(queueKey(count).c_str(),e)!=e.length()) return false;
  return queuePrefs.putUChar("count",count+1)==sizeof(uint8_t);
}
bool popRecord() {
  uint8_t count=pendingCount(); if (!count) return true;
  for (uint8_t i=1;i<count;++i) queuePrefs.putString(queueKey(i-1).c_str(),queuePrefs.getString(queueKey(i).c_str(),""));
  queuePrefs.remove(queueKey(count-1).c_str());
  return queuePrefs.putUChar("count",count-1)==sizeof(uint8_t);
}

bool connectSta(uint32_t timeoutMs) {
  if (!strlen(WIFI_SSID)) return false;
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  uint32_t start=millis();
  while (WiFi.status()!=WL_CONNECTED && uint32_t(millis()-start)<timeoutMs) delay(50);
  if (WiFi.status()==WL_CONNECTED) { netMode=NetMode::Sta; return true; }
  WiFi.disconnect(true,false); WiFi.mode(WIFI_OFF); netMode=NetMode::Off; return false;
}
void syncClock() {
  if (timeValid() || WiFi.status()!=WL_CONNECTED) return;
  configTzTime("UTC0","time.google.com","pool.ntp.org");
  uint32_t start=millis(); while (!timeValid() && uint32_t(millis()-start)<Config::NTP_TIMEOUT_MS) delay(50);
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
  if (!pendingCount()) return;
  if (netMode!=NetMode::Sta && !connectSta(Config::WIFI_TIMEOUT_MS)) return;
  syncClock();
  while (pendingCount()) {
    SessionRecord r; if (!decodeRecord(queuePrefs.getString("q0",""),r)) break;
    if (!uploadRecord(r)) { logLive("upload", "failed"); break; }
    logLive("upload", "ok");
    if (!popRecord()) break;
  }
}

void rotateDiagIfNeeded() {
  if (!spiffsReady) return;
  File f=SPIFFS.open("/diag.jsonl",FILE_READ); size_t size=f?f.size():0; if(f)f.close();
  if (size < Config::DIAG_ROTATE_BYTES) return;
  SPIFFS.remove("/diag.prev.jsonl"); SPIFFS.rename("/diag.jsonl","/diag.prev.jsonl");
}
void appendDiag(const String &line) {
  if (!spiffsReady) return; rotateDiagIfNeeded();
  File f=SPIFFS.open("/diag.jsonl",FILE_APPEND); if(!f)return; f.println(line); f.close();
}
void saveTrace(const String &sessionId) {
  if (!spiffsReady) return;
  uint8_t slot=diagPrefs.getUChar("slot",0)%Config::TRACE_SLOTS;
  String path="/trace"+String(slot)+".csv";
  File f=SPIFFS.open(path,FILE_WRITE); if(!f)return;
  f.println("session_id,"+sessionId); f.println("S,uptime_ms,distance_mm,flags(valid=1 blocked=2 rfid=4)");
  for(uint16_t i=0;i<traceCount;++i){const TraceSample&s=traceSamples[(traceHead+i)%TRACE_CAP]; f.printf("S,%lu,%u,%u\n",(unsigned long)s.ms,s.mm,s.flags);} 
  f.println("E,uptime_ms,code,value,aux");
  for(uint8_t i=0;i<traceEventCount;++i){const TraceEvent&e=traceEvents[i];f.printf("E,%lu,%u,%u,%u\n",(unsigned long)e.ms,e.code,e.value,e.aux);} f.close();
  diagPrefs.putUChar("slot",(slot+1)%Config::TRACE_SLOTS);
}

const char *phaseName() {
  switch(visit.phase){case Visit::Phase::Idle:return "idle";case Visit::Phase::Candidate:return "candidate";case Visit::Phase::Entry:return "entry";case Visit::Phase::Exit:return "exit";case Visit::Phase::Complete:return "complete";case Visit::Phase::WaitClear:return "wait_clear";} return "?";
}

String statusJson() {
  String j="{\"firmware\":\""+String(FW_VERSION)+"\",\"state\":\""+phaseName()+"\",";
  j+="\"distance_mm\":"+String(latestDistance==UINT16_MAX?-1:latestDistance)+",\"rfid_on\":"+(rfidPower?"true":"false")+",";
  j+="\"pending\":"+String(pendingCount())+",\"network\":\""+(netMode==NetMode::Sta?"sta":netMode==NetMode::Ap?"ap":"off")+"\"}";
  return j;
}

String diagIndexHtml() {
  String h="<!doctype html><meta name='viewport' content='width=device-width'><h1>診斷資料</h1><p><a href='/diag/current'>目前 JSONL</a> · <a href='/diag/previous'>上一輪 JSONL</a></p><ul>";
  for(uint8_t i=0;i<Config::TRACE_SLOTS;++i)h+="<li><a href='/trace?slot="+String(i)+"'>trace "+String(i)+"</a></li>";
  return h+"</ul><p><a href='/'>返回</a></p>";
}

String liveLogText() {
  String out; out.reserve(8000);
  uint32_t total = min(liveSeq, (uint32_t)LIVE_LOG_CAP);
  uint32_t first = liveSeq > LIVE_LOG_CAP ? liveSeq - LIVE_LOG_CAP : 0;
  for (uint32_t seq = first; seq < first + total; ++seq) {
    const LiveLog &r = liveLogs[seq % LIVE_LOG_CAP];
    out += String(r.ms) + "\t" + r.action + "\t" + r.detail + "\n";
  }
  return out;
}

String mainHtml() {
  return String("<!doctype html><html lang='zh-Hant'><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font-family:system-ui;margin:20px}.box{padding:14px;border:1px solid #bbb;margin:12px 0;max-width:760px}button,input{font-size:16px;padding:10px;margin:6px}</style><h1>Smart Litter Cabinet</h1><div id='s' class='box'>讀取中…</div><p><a href='/diagnostics'>診斷資料</a> · <a href='/live'>即時 Log</a></p><div class='box'><h2>Web OTA</h2><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='firmware' accept='.bin,application/octet-stream' required><button>上傳並更新</button></form><p>只選 Arduino 匯出的 <code>*.ino.bin</code>。</p></div><form method='post' action='/normal'><button>關閉維護模式，回低功耗</button></form><script>async function u(){try{let r=await fetch('/api/status',{cache:'no-store'});s.textContent=JSON.stringify(await r.json(),null,2)}catch(e){s.textContent=e}setTimeout(u,1000)}u()</script></html>");
}

void sendFilePath(const String &path,const char *type) {
  if(!spiffsReady||!SPIFFS.exists(path)){web.send(404,"text/plain","not found");return;} File f=SPIFFS.open(path,FILE_READ);web.streamFile(f,type);f.close();
}

void registerWebRoutes() {
  if(webRoutesReady)return; webRoutesReady=true;
  web.on("/",HTTP_GET,[](){web.send(200,"text/html; charset=utf-8",mainHtml());});
  web.on("/api/status",HTTP_GET,[](){web.sendHeader("Cache-Control","no-store");web.send(200,"application/json",statusJson());});
  web.on("/diagnostics",HTTP_GET,[](){web.send(200,"text/html; charset=utf-8",diagIndexHtml());});
  web.on("/live",HTTP_GET,[](){web.sendHeader("Cache-Control","no-store");web.send(200,"text/plain; charset=utf-8",liveLogText());});
  web.on("/diag/current",HTTP_GET,[](){sendFilePath("/diag.jsonl","application/x-ndjson");});
  web.on("/diag/previous",HTTP_GET,[](){sendFilePath("/diag.prev.jsonl","application/x-ndjson");});
  web.on("/trace",HTTP_GET,[](){int slot=web.hasArg("slot")?web.arg("slot").toInt():-1;if(slot<0||slot>=Config::TRACE_SLOTS){web.send(400,"text/plain","bad slot");return;}sendFilePath("/trace"+String(slot)+".csv","text/csv");});
  web.on("/normal",HTTP_POST,[](){web.send(200,"text/plain; charset=utf-8","即將回低功耗模式");maintenanceSticky=false;maintenanceDuration=1;maintenanceStarted=0;});
  web.on("/update",HTTP_POST,[](){bool ok=otaStarted&&!otaFailed&&!Update.hasError();web.sendHeader("Connection","close");web.send(ok?200:500,"text/plain; charset=utf-8",ok?"更新成功，重新啟動":"更新失敗，保留原韌體");if(ok){delay(400);ESP.restart();}otaStarted=otaFailed=false;},[](){HTTPUpload&u=web.upload();if(u.status==UPLOAD_FILE_START){otaStarted=true;otaFailed=false;rfidSet(false);if(!Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH))otaFailed=true;}else if(u.status==UPLOAD_FILE_WRITE){if(!otaFailed&&Update.write(u.buf,u.currentSize)!=u.currentSize)otaFailed=true;}else if(u.status==UPLOAD_FILE_END){if(!otaFailed&&!Update.end(true))otaFailed=true;}else if(u.status==UPLOAD_FILE_ABORTED){otaFailed=true;Update.abort();}});
  web.onNotFound([](){web.send(404,"text/plain","not found");});
}

void startMaintenance(uint32_t durationMs) {
  if(webRunning){maintenanceStarted=millis();maintenanceDuration=durationMs;return;}
  if(netMode!=NetMode::Sta && !connectSta(Config::WIFI_TIMEOUT_MS)) { WiFi.mode(WIFI_AP); WiFi.softAP("LitterCabinet"); netMode=NetMode::Ap; }
  if(netMode==NetMode::Sta) syncClock();
  registerWebRoutes(); web.begin(); webRunning=true; maintenanceStarted=millis(); maintenanceDuration=durationMs; maintenanceSticky=DEBUG_WEB_SERVER;
  logLive("web",netMode==NetMode::Sta?WiFi.localIP().toString():WiFi.softAPIP().toString());
}
void stopMaintenance() {
  if(!webRunning)return; web.stop(); webRunning=false;
  if(netMode==NetMode::Ap)WiFi.softAPdisconnect(true); else if(netMode==NetMode::Sta)WiFi.disconnect(true,false);
  WiFi.mode(WIFI_OFF);netMode=NetMode::Off;
}
void serviceMaintenance() {
  if(!webRunning)return; web.handleClient();
  if(!maintenanceSticky && uint32_t(millis()-maintenanceStarted)>=maintenanceDuration)stopMaintenance();
}

void saveDiagnostics(const String &sid, const String &chip, const String &cat, Visit::Reason reason, uint32_t durationMs) {
  uint32_t avg=metrics.samples?(uint32_t)(metrics.sumMm/metrics.samples):0;
  String j="{\"fw\":\""+String(FW_VERSION)+"\",\"session_id\":\""+jsonEscape(sid)+"\",\"reason\":\""+Visit::reasonName(reason)+"\",";
  j+="\"cat\":\""+jsonEscape(cat)+"\",\"duration_ms\":"+String(durationMs)+",\"samples\":"+String(metrics.samples)+",\"invalid\":"+String(metrics.invalid)+",\"max_gap_ms\":"+String(metrics.maxGap)+",";
  j+="\"min_mm\":"+String(metrics.minMm==UINT16_MAX?0:metrics.minMm)+",\"avg_mm\":"+String(avg)+",\"scans\":"+String(metrics.scanCount)+",\"recognized\":"+String(metrics.recognized)+",";
  j+="\"rfid_on_ms\":"+String(metrics.rfidOnMs)+",\"first_byte_ms\":"+String(metrics.firstByteMinMs==UINT32_MAX?0:metrics.firstByteMinMs)+",\"valid_chip_ms\":"+String(metrics.validMinMs==UINT32_MAX?0:metrics.validMinMs)+",";
  j+="\"bytes\":"+String(metrics.bytes)+",\"bad_frames\":"+String(metrics.badFrames)+",\"sleep_ms\":"+String(metrics.sleepMs)+",\"sleep_calls\":"+String(metrics.sleepCalls)+",\"sleep_errors\":"+String(metrics.sleepErrors)+"}";
  appendDiag(j); saveTrace(sid);
}

void finishSession() {
  rfidSet(false);
  String chip=String(visit.chip), cat=catForChip(chip), sid=newSessionId();
  uint32_t durationMs=visit.durationMs(); Visit::Reason reason=visit.reason;
  saveDiagnostics(sid,chip,cat,reason,durationMs);
  logLive("session_end", String(Visit::reasonName(reason)) + " " + String(durationMs) + "ms");

  bool trustworthy=reason==Visit::Reason::Normal && durationMs>0 && registeredChip(chip) && !visit.conflict;
  SessionRecord record;
  if(trustworthy){
    record.session_id=sid;record.chip_id=chip;record.cat_id=cat;record.duration_sec=max(1UL,durationMs/1000UL);
    record.min_distance_mm=metrics.minMm==UINT16_MAX?0:metrics.minMm;
    record.avg_distance_mm=metrics.samples?(float)metrics.sumMm/metrics.samples:0;
    record.sample_count=metrics.samples;
    if(netMode!=NetMode::Sta)connectSta(Config::WIFI_TIMEOUT_MS);if(netMode==NetMode::Sta)syncClock();
    record.enter_time=epochForUptime(visit.started);record.exit_time=epochForUptime(visit.exitStarted);
    if(!enqueueRecord(record))logLive("queue","full/write failure");
  } else logLive("discard",Visit::reasonName(reason));

  visit.release(); metrics.open=false; traceReset();
  uploadPending();
  startMaintenance(Config::POST_EVENT_MAINTENANCE_MS);
}

void processSensor() {
  uint32_t now=millis();
  uint32_t period=(visit.phase==Visit::Phase::Idle||visit.phase==Visit::Phase::WaitClear)?Config::IDLE_RANGING_PERIOD_MS:Config::ACTIVE_RANGING_PERIOD_MS;
  if(lastRangeMs && uint32_t(now-lastRangeMs)<period)return;
  lastRangeMs=now; uint16_t mm=readDistance(); latestDistance=mm; now=millis();
  Visit::Phase before=visit.phase;
  visit.sample(now,mm!=UINT16_MAX,mm);
  if(before==Visit::Phase::Idle && visit.phase==Visit::Phase::Candidate) metricsBegin(now,mm);
  else if(metrics.open) metricsSample(now,mm);
  syncRfidPower();
  if(visit.phase==Visit::Phase::Complete)finishSession();
}

void maybeSleep() {
  if(webRunning||netMode!=NetMode::Off||rfidPower)return;
  uint32_t period=(visit.phase==Visit::Phase::Idle||visit.phase==Visit::Phase::WaitClear)?Config::IDLE_RANGING_PERIOD_MS:Config::ACTIVE_RANGING_PERIOD_MS;
  uint32_t now=millis(), elapsed=uint32_t(now-lastRangeMs); if(elapsed+5>=period)return;
  uint32_t ms=period-elapsed-2; int64_t before=esp_timer_get_time();
  esp_err_t a=esp_sleep_enable_timer_wakeup((uint64_t)ms*1000ULL);esp_err_t r=a==ESP_OK?esp_light_sleep_start():a;
  if(metrics.open){if(r==ESP_OK){++metrics.sleepCalls;metrics.sleepMs+=(uint32_t)((esp_timer_get_time()-before)/1000);}else ++metrics.sleepErrors;}
}

void setup() {
  Serial.begin(115200);
  pinMode(Config::RFID_ENABLE_PIN,OUTPUT);digitalWrite(Config::RFID_ENABLE_PIN,LOW);
  pinMode(Config::TOF_INT_PIN,INPUT_PULLUP);
  rfidSerial.setRxBufferSize(512);rfidSerial.begin(9600,SERIAL_8N1,Config::RFID_RX_PIN,-1);
  queuePrefs.begin("litter-v2",false);diagPrefs.begin("litter-diag",false);
  spiffsReady=SPIFFS.begin(false);
  initTof();
  startMaintenance(Config::BOOT_MAINTENANCE_MS);
  if(netMode==NetMode::Sta)uploadPending();
  logLive("ready",FW_VERSION);
}

void loop() {
  serviceMaintenance();
  pollRfid();
  processSensor();
  pollRfid();
  maybeSleep();
  delay(1);
}
