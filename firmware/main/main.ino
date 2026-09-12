#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <VL53L0X.h>
#include "esp_sleep.h"
#include "esp_system.h"
#include <time.h>
#include <stdlib.h>
#include "app_config.h"
#include "certificates.h"
#include "visit_logic.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// Keep secrets.h ONLY on the local computer used to compile/flash the board.
// Do not add it to Cirkit or any shared/cloud project.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef DEBUG_WEB_SERVER
#define DEBUG_WEB_SERVER false
#endif



struct SessionRecord {
  String session_id;
  String chip_id;
  String cat_id;
  String enter_time;
  String exit_time;
  uint32_t duration_sec = 0;
  uint16_t min_distance_mm = UINT16_MAX;
  float avg_distance_mm = 0;
  uint32_t sample_count = 0;
};

VL53L0X tof;
HardwareSerial rfidSerial(1);
Preferences prefs;
WebServer debugServer(80);
Visit::Engine visit;
SessionRecord currentSession;
uint32_t lastRangeMs = 0;
bool ranged = false;
uint32_t maxRangeGapMs = 0;
uint32_t seenScanGeneration = 0;
uint32_t failedRecords = 0;
uint32_t storageErrors = 0;
bool journalWritable = true;
String recentReason = "none";
String recentState = "尚無紀錄";
String recentSessionId;
time_t eventStartUtc = 0;
time_t candidateStartUtc = 0;
uint16_t candidateFirstDistance = UINT16_MAX;
struct UploadJob { char id[81]; char json[2048]; };
struct UploadResult { char id[81]; int status; bool ok; };
QueueHandle_t uploadJobs = nullptr;
QueueHandle_t uploadResults = nullptr;
bool uploadBusy = false;
bool retryScheduled = false;
uint32_t lastUploadAttempt = 0;
uint32_t sessionStartMs = 0;
uint64_t distanceSumMm = 0;
uint32_t invalidDistanceSinceMs = 0;
uint32_t lastCheckpointMs = 0;
uint32_t lastTofInitAttemptMs = 0;
bool tofInitialized = false;
uint16_t latestDistanceMm = UINT16_MAX;
bool rfidEnabled = false;
bool rfidScanActive = false;
bool rfidReceiving = false;
bool debugServerStarted = false;
uint32_t rfidScanStartedMs = 0;
uint32_t rfidBytesSeen = 0;
uint32_t rfidInvalidFrames = 0;
int lastUploadStatus = 0;
bool lastUploadOk = false;
String lastUploadResponse;
String rfidFrameBuffer;
String rfidLastRaw;
String rfidLastStatus = "尚未掃描";
String rfidScanLabel = "掃描";

void serviceDebugServer();
bool checkpointActiveSession();
void beginSession(uint16_t distance);

uint32_t fnv1a32(const String &text) {
  uint32_t hash = 0x811C9DC5;
  for (size_t i = 0; i < text.length(); ++i) {
    hash ^= static_cast<uint8_t>(text[i]);
    hash *= 0x01000193;
  }
  return hash;
}

String catNameForChip(const String &chipId) {
  if (CAT_1_CHIP_RAW[0] != '\0' && chipId == CAT_1_CHIP_RAW) return CAT_1_NAME;
  if (CAT_2_CHIP_RAW[0] != '\0' && chipId == CAT_2_CHIP_RAW) return CAT_2_NAME;

  return "";
}

bool isRegisteredChip(const String &chipId) {
  return (CAT_1_CHIP_RAW[0] != '\0' && chipId == CAT_1_CHIP_RAW) ||
         (CAT_2_CHIP_RAW[0] != '\0' && chipId == CAT_2_CHIP_RAW);
}

void rfidOff() {
  digitalWrite(Config::RFID_ENABLE_PIN, LOW);
  rfidEnabled = false;
  rfidScanActive = false;
}

void rfidOn() {
  while (rfidSerial.available()) rfidSerial.read();
  digitalWrite(Config::RFID_ENABLE_PIN, HIGH);
  rfidEnabled = true;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
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
    if (c == '.' && !sawDot && i > 0 && i + 1 < value.length()) {
      sawDot = true;
      continue;
    }
    return false;
  }
  char *end = nullptr;
  const float parsed = strtof(value.c_str(), &end);
  if (end != value.c_str() + value.length() || !isfinite(parsed)) return false;
  result = parsed;
  return true;
}

bool parseRfidFrame(const String &frame, String &chipId) {
  // Payload without '$'/'#': F + 15 decimal digits + 2 hex checksum = 18 chars.
  if (frame.length() != 18 || frame[0] != 'F') return false;
  for (uint8_t i = 1; i <= 15; ++i) {
    if (!isDigit(frame[i])) return false;
  }
  const int hi = hexNibble(frame[16]);
  const int lo = hexNibble(frame[17]);
  if (hi < 0 || lo < 0) return false;
  uint8_t calculated = 0;
  for (uint8_t i = 0; i <= 15; ++i) calculated ^= static_cast<uint8_t>(frame[i]);
  if (calculated != static_cast<uint8_t>((hi << 4) | lo)) return false;
  chipId = frame.substring(1, 16);
  return true;
}

void syncRfidScan() {
  if (visit.scanGeneration != seenScanGeneration) {
    rfidOff();
    seenScanGeneration = visit.scanGeneration;
    rfidReceiving = false;
    rfidFrameBuffer = "";
    rfidBytesSeen = 0;
    rfidInvalidFrames = 0;
    rfidLastRaw = "";
    rfidScanLabel = visit.exitScan ? "離開掃描" : "入口候選掃描";
    if (visit.scanning) rfidOn();
  }
  rfidScanActive = visit.scanning;
  if (!visit.scanning && rfidEnabled) rfidOff();
  switch (visit.scanResult) {
    case Visit::ScanResult::Scanning: rfidLastStatus = "掃描中"; break;
    case Visit::ScanResult::Valid: rfidLastStatus = visit.conflict ? "有效晶片與事件身分衝突" : "已取得有效晶片"; break;
    case Visit::ScanResult::Rejected: rfidLastStatus = "未登錄晶片，已立即拒絕"; break;
    case Visit::ScanResult::Timeout:
      rfidLastStatus = visit.chip[0] ? "10秒內未取得有效封包，保留事件身分"
                                     : "10秒內未取得已登錄晶片，未建立事件";
      break;
    case Visit::ScanResult::Cancelled: rfidLastStatus = "事件期限到，保留事件身分"; break;
    default: rfidLastStatus = "尚未掃描";
  }
}

void pollRfidScan() {
  visit.tick(millis());
  syncRfidScan();
  // Bound each UART service pass; noise cannot monopolize the sensor loop.
  for (unsigned budget = 0; budget < 96 && visit.scanning && rfidSerial.available(); ++budget) {
    const char c = static_cast<char>(rfidSerial.read());
    ++rfidBytesSeen;
    if (c == '$') { rfidFrameBuffer = ""; rfidReceiving = true; }
    else if (c == '#' && rfidReceiving) {
      String chipId;
      if (parseRfidFrame(rfidFrameBuffer, chipId) && isRegisteredChip(chipId)) {
        const bool wasCandidate = visit.candidate();
        if (!visit.acceptChip(millis(), chipId.c_str())) {
          ++rfidInvalidFrames;
          rfidLastRaw = "有效封包但掃描已結束";
          rfidReceiving = false;
          continue;
        }
        if (wasCandidate) beginSession(candidateFirstDistance);
        currentSession.chip_id = visit.chip;
        currentSession.cat_id = catNameForChip(currentSession.chip_id);
        rfidLastRaw = "有效封包（晶片已遮罩）";
        checkpointActiveSession();
        Serial.printf("RFID valid=yes identity_retained=yes conflict=%s\n", visit.conflict ? "yes" : "no");
      } else if (chipId.length() == 15 && visit.rejectChip(millis(), chipId.c_str())) {
        candidateStartUtc = 0;
        candidateFirstDistance = UINT16_MAX;
        rfidLastRaw = "未登錄晶片（已拒絕，不公開原文）";
        Serial.println("RFID registered=no action=rejected");
      } else {
        ++rfidInvalidFrames;
        rfidLastRaw = "無效封包（原文不公開）";
      }
      rfidReceiving = false;
    } else if (rfidReceiving) {
      if (rfidFrameBuffer.length() < 24) rfidFrameBuffer += c;
      else { ++rfidInvalidFrames; rfidReceiving = false; }
    }
  }
  syncRfidScan();
}

bool initToF() {
  lastTofInitAttemptMs = millis();
  Wire.begin(Config::TOF_SDA_PIN, Config::TOF_SCL_PIN);
  Wire.setClock(400000);
  tof.setTimeout(100);
  tofInitialized = tof.init();
  if (!tofInitialized) return false;
  if (!tof.setMeasurementTimingBudget(Config::TOF_TIMING_BUDGET_US)) {
    tofInitialized = false;
    return false;
  }
  return true;
}

uint16_t readDistanceMm() {
  if (!tofInitialized) {
    latestDistanceMm = UINT16_MAX;
    return UINT16_MAX;
  }
  const uint16_t distance = tof.readRangeSingleMillimeters();
  latestDistanceMm = (tof.timeoutOccurred() || distance == 0 || distance > 2000)
                         ? UINT16_MAX
                         : distance;
  return latestDistanceMm;
}



void resetSession() {
  currentSession = SessionRecord{};
  distanceSumMm = 0;
  sessionStartMs = 0;
  lastCheckpointMs = 0;
  eventStartUtc = 0;
}

void addDistanceSample(uint16_t distance) {
  if (distance == UINT16_MAX) return;
  currentSession.min_distance_mm = min(currentSession.min_distance_mm, distance);
  distanceSumMm += distance;
  currentSession.sample_count++;
}

String createSessionId() {
  const uint64_t mac = ESP.getEfuseMac();
  char value[48];
  snprintf(value, sizeof(value), "%08lX-%08lX-%06llX",
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(millis()),
           static_cast<unsigned long long>(mac & 0xFFFFFFULL));
  return String(value);
}

String formatIsoUtc(time_t epoch) {
  struct tm tmValue;
  gmtime_r(&epoch, &tmValue);
  char output[25];
  strftime(output, sizeof(output), "%Y-%m-%dT%H:%M:%SZ", &tmValue);
  return String(output);
}

bool timeIsValid() {
  return time(nullptr) > 1700000000;
}

bool connectWifiAndTryTime() {
  if (strlen(WIFI_SSID) == 0 || strlen(WIFI_PASSWORD) == 0 ||
      strlen(DEVICE_TOKEN) == 0 || strlen(APP_SCRIPT_URL) == 0) {
    Serial.println("UPLOAD skipped: local secrets.h is not configured");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < Config::WIFI_TIMEOUT_MS) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
  if (WiFi.status() != WL_CONNECTED) return false;

  if (!timeIsValid()) {
    configTzTime("UTC0", "time.google.com", "pool.ntp.org");
    const uint32_t ntpStarted = millis();
    while (!timeIsValid() && millis() - ntpStarted < Config::NTP_TIMEOUT_MS) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
  return true;
}

void disconnectWifi() {
  if (DEBUG_WEB_SERVER) return;
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}



String jsonEscape(const String &input) {
  String output;
  output.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    if (c == '\\' || c == '"') { output += '\\'; output += c; }
    else if (c == '\n') output += "\\n";
    else if (c == '\r') output += "\\r";
    else if (static_cast<uint8_t>(c) >= 0x20) output += c;
  }
  return output;
}

String recordJson(const SessionRecord &record) {
  String json;
  json.reserve(700);
  json += "{\"device_token\":\"" + jsonEscape(String(DEVICE_TOKEN)) + "\",\"session\":{";
  json += "\"session_id\":\"" + jsonEscape(record.session_id) + "\",";
  json += "\"chip_id\":\"" + jsonEscape(record.chip_id) + "\",";
  json += "\"cat_id\":\"" + jsonEscape(record.cat_id) + "\",";
  json += "\"enter_time\":\"" + jsonEscape(record.enter_time) + "\",";
  json += "\"exit_time\":\"" + jsonEscape(record.exit_time) + "\",";
  json += "\"duration_sec\":" + String(record.duration_sec) + ",";
  json += "\"min_distance_mm\":" + String(record.min_distance_mm) + ",";
  json += "\"avg_distance_mm\":" + String(record.avg_distance_mm, 1) + ",";
  json += "\"sample_count\":" + String(record.sample_count);
  json += "}}";
  return json;
}

UploadResult uploadRecord(const UploadJob &job) {
  UploadResult result{};
  strlcpy(result.id, job.id, sizeof(result.id));
  result.status = -1;
  if (WiFi.status() != WL_CONNECTED) return result;
  WiFiClientSecure postClient;
  postClient.setCACert(GOOGLE_ROOT_CA_BUNDLE);
  HTTPClient postHttp;
  postHttp.setConnectTimeout(10000);
  postHttp.setTimeout(15000);
  postHttp.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!postHttp.begin(postClient, APP_SCRIPT_URL)) {
    result.status = -1;
    return result;
  }
  postHttp.addHeader("Content-Type", "application/json");
  const int postStatus = postHttp.POST(String(job.json));
  String response = postStatus > 0 ? postHttp.getString() : postHttp.errorToString(postStatus);
  const String redirectUrl = postHttp.getLocation();
  postHttp.end();

  int status = postStatus;
  if (postStatus == HTTP_CODE_FOUND || postStatus == HTTP_CODE_SEE_OTHER) {
    if (!redirectUrl.startsWith("https://")) {
      result.status = postStatus;
    return result;
    }

    // Apps Script ContentService returns its JSON through a one-time
    // script.googleusercontent.com URL. Use a fresh TLS connection and GET;
    // reusing the POST client across hosts produces an invalid request.
    WiFiClientSecure resultClient;
    resultClient.setCACert(GOOGLE_ROOT_CA_BUNDLE);
    HTTPClient resultHttp;
    resultHttp.setConnectTimeout(10000);
    resultHttp.setTimeout(15000);
    resultHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!resultHttp.begin(resultClient, redirectUrl)) {
      result.status = -1;
    return result;
    }
    status = resultHttp.GET();
    response = status > 0 ? resultHttp.getString() : resultHttp.errorToString(status);
    resultHttp.end();
  }

  const bool ok = status >= 200 && status < 300 && response.indexOf("\"ok\":true") >= 0;
  result.status = status;
  result.ok = ok;
  return result;
}

// Only this worker owns networking. Queues copy fixed-size snapshots, never String pointers.
void uploadWorker(void *) {
  UploadJob job{};
  while (true) {
    if (xQueueReceive(uploadJobs, &job, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
    UploadResult result{};
    strlcpy(result.id, job.id, sizeof(result.id));
    result.status = -1;
    if (connectWifiAndTryTime()) result = uploadRecord(job);
    disconnectWifi();
    xQueueSend(uploadResults, &result, portMAX_DELAY);
  }
}

String encodeRecord(const SessionRecord &r) {
  const char sep = 0x1F;
  return r.session_id + sep + r.chip_id + sep + r.cat_id + sep + r.enter_time + sep +
         r.exit_time + sep + String(r.duration_sec) + sep + String(r.min_distance_mm) + sep +
         String(r.avg_distance_mm, 1) + sep + String(r.sample_count);
}

bool decodeRecord(const String &encoded, SessionRecord &r) {
  String fields[9];
  uint8_t field = 0;
  for (size_t i = 0; i <= encoded.length(); ++i) {
    if (i == encoded.length() || encoded[i] == 0x1F) {
      if (++field > 9) return false;
    } else {
      if (field >= 9) return false;
      fields[field] += encoded[i];
    }
  }
  if (field != 9) return false;
  uint32_t durationSec = 0;
  uint32_t minDistanceMm = 0;
  uint32_t sampleCount = 0;
  float avgDistanceMm = 0;
  if (!parseDecimal(fields[5], durationSec) ||
      !parseDecimal(fields[6], minDistanceMm) ||
      !parseFiniteDecimalFloat(fields[7], avgDistanceMm) ||
      !parseDecimal(fields[8], sampleCount) ||
      fields[0].length() == 0 || fields[0].length() > 80 ||
      fields[1].length() > 64 || fields[2].length() > 64 ||
      durationSec < 1 || durationSec > 86400 ||
      minDistanceMm < 1 || minDistanceMm > 2000 ||
      avgDistanceMm < 1 || avgDistanceMm > 2000 ||
      sampleCount < 1 || sampleCount > 100000) {
    return false;
  }
  r.session_id = fields[0]; r.chip_id = fields[1]; r.cat_id = fields[2];
  r.enter_time = fields[3]; r.exit_time = fields[4];
  r.duration_sec = durationSec;
  r.min_distance_mm = static_cast<uint16_t>(minDistanceMm);
  r.avg_distance_mm = avgDistanceMm;
  r.sample_count = sampleCount;
  return true;
}

String queueKey(uint8_t index) {
  return "q" + String(index);
}

String queueMetaPayload(uint8_t head, uint8_t count) {
  return "1:" + String(head) + ":" + String(count);
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

bool storeQueueMeta(uint8_t head, uint8_t count) {
  if (head >= Config::MAX_PENDING_RECORDS || count > Config::MAX_PENDING_RECORDS) return false;
  const String payload = queueMetaPayload(head, count);
  const String encoded = payload + ":" + String(fnv1a32(payload), HEX);
  const size_t written = prefs.putString("qmeta", encoded);
  if (written != encoded.length()) {
    Serial.println("PENDING queue metadata write failed");
    return false;
  }
  return true;
}

bool loadQueueMeta(uint8_t &head, uint8_t &count) {
  const String encoded = prefs.getString("qmeta", "");
  if (!encoded.length()) {
    // The old queue was q0..qN with no head; it is already a ring at head 0.
    const uint8_t legacyCount = prefs.getUChar("qcount", 0);
    count = min(legacyCount, Config::MAX_PENDING_RECORDS);
    head = 0;
    if (legacyCount > Config::MAX_PENDING_RECORDS) {
      Serial.println("PENDING legacy count clamped to capacity");
    }
    return storeQueueMeta(head, count);
  }

  String fields[4];
  uint8_t field = 0;
  for (size_t i = 0; i <= encoded.length(); ++i) {
    if (i == encoded.length() || encoded[i] == ':') {
      if (++field > 4) break;
    } else if (field < 4) {
      fields[field] += encoded[i];
    }
  }
  uint32_t parsedHead = 0;
  uint32_t parsedCount = 0;
  uint32_t storedChecksum = 0;
  const bool valid = field == 4 && fields[0] == "1" &&
                     parseDecimal(fields[1], parsedHead) &&
                     parseDecimal(fields[2], parsedCount) &&
                     parseHex32(fields[3], storedChecksum) &&
                     parsedHead < Config::MAX_PENDING_RECORDS &&
                     parsedCount <= Config::MAX_PENDING_RECORDS &&
                     storedChecksum == fnv1a32("1:" + fields[1] + ":" + fields[2]);
  if (!valid) {
    Serial.println("PENDING queue metadata invalid; preserving all slots");
    head = 0;
    count = 0;
    return false;
  }
  head = static_cast<uint8_t>(parsedHead);
  count = static_cast<uint8_t>(parsedCount);
  return true;
}

bool enqueuePending(const SessionRecord &record) {
  uint8_t head = 0;
  uint8_t count = 0;
  if (!loadQueueMeta(head, count)) return false;
  if (count >= Config::MAX_PENDING_RECORDS) {
    Serial.println("PENDING queue full; new record cannot be saved");
    return false;
  }
  const uint8_t tail = (head + count) % Config::MAX_PENDING_RECORDS;
  const String encoded = encodeRecord(record);
  const size_t written = prefs.putString(queueKey(tail).c_str(), encoded);
  if (written != encoded.length()) {
    Serial.println("PENDING record write failed");
    return false;
  }
  // qmeta is one NVS value, so count/head become visible atomically. If power
  // fails before this write, the slot is not queued; report failure, never replay the journal.
  if (!storeQueueMeta(head, count + 1)) return false;
  Serial.printf("PENDING queued=%u\n", count + 1);
  return true;
}

bool removeOldestPending() {
  uint8_t head = 0;
  uint8_t count = 0;
  if (!loadQueueMeta(head, count)) return false;
  if (!count) return true;
  // Do not physically shift or delete records. Updating the single metadata
  // value first makes an interrupted dequeue replay at worst, never reorder
  // the remaining records or discard an unsent tail record.
  return storeQueueMeta((head + 1) % Config::MAX_PENDING_RECORDS, count - 1);
}

void storageFailure(const char *message) {
  ++storageErrors;
  recentState = message;
  Serial.printf("STORAGE error: %s\n", message);
}

bool checkpointActiveSession() {
  if (!journalWritable || !visit.active() || !currentSession.session_id.length()) return false;
  // Journal is diagnostic only after reboot: never replay it as a completed visit.
  SessionRecord snapshot = currentSession;
  snapshot.duration_sec = max(1UL, visit.elapsed(millis()) / 1000UL);
  snapshot.avg_distance_mm = snapshot.sample_count
    ? static_cast<float>(distanceSumMm) / snapshot.sample_count : 0;
  const String encoded = String("v2|") + (visit.conflict ? "conflict|" : "incomplete|") + encodeRecord(snapshot);
  if (prefs.putString("active", encoded) != encoded.length()) {
    storageFailure("活動備份失敗"); return false;
  }
  lastCheckpointMs = millis();
  return true;
}

void recoverJournal() {
  if (!prefs.isKey("active")) return;
  // Preserve one interrupted journal without guessing elapsed powered-off time.
  if (prefs.isKey("interrupted")) {
    journalWritable = false;
    storageFailure("中斷紀錄槽已滿，保留原備份並停用活動備份");
    return;
  }
  const String saved = prefs.getString("active", "");
  if (!saved.length() || prefs.putString("interrupted", saved) != saved.length()) {
    journalWritable = false; storageFailure("中斷紀錄保存失敗"); return;
  }
  if (!prefs.remove("active")) {
    journalWritable = false; storageFailure("舊活動備份清除失敗"); return;
  }
  recentState = "重啟中斷紀錄已保留本機，不上傳";
}

void serviceUploads() {
  if (!uploadJobs || !uploadResults) return;
  UploadResult result{};
  if (xQueueReceive(uploadResults, &result, 0) == pdTRUE) {
    uploadBusy = false;
    lastUploadStatus = result.status;
    lastUploadOk = result.ok;
    lastUploadResponse = result.ok ? "上傳成功" : "上傳失敗，保留待傳紀錄";
    bool acknowledged = result.ok;
    if (result.ok) {
      uint8_t head = 0, count = 0;
      SessionRecord queued;
      if (!loadQueueMeta(head, count) || !count ||
          !decodeRecord(prefs.getString(queueKey(head).c_str(), ""), queued) ||
          queued.session_id != result.id || !removeOldestPending()) {
        storageFailure("上傳成功但佇列確認失敗，保留重試");
        acknowledged = false;
      } else if (recentSessionId == result.id) recentState = "上傳成功";
    }
    retryScheduled = !acknowledged;
    lastUploadAttempt = millis();
  }
  if (uploadBusy || (retryScheduled && uint32_t(millis() - lastUploadAttempt) < 60000)) return;
  uint8_t head = 0, count = 0;
  if (!loadQueueMeta(head, count)) { retryScheduled = true; lastUploadAttempt = millis(); storageFailure("待傳索引損毀"); return; }
  if (!count) return;
  SessionRecord queued;
  if (!decodeRecord(prefs.getString(queueKey(head).c_str(), ""), queued)) {
    retryScheduled = true; lastUploadAttempt = millis(); storageFailure("待傳紀錄損毀，保留資料"); return;
  }
  UploadJob job{};
  const String json = recordJson(queued);
  if (json.length() >= sizeof(job.json)) {
    retryScheduled = true; lastUploadAttempt = millis(); storageFailure("上傳快照超出容量"); return;
  }
  strlcpy(job.id, queued.session_id.c_str(), sizeof(job.id));
  strlcpy(job.json, json.c_str(), sizeof(job.json));
  if (xQueueSend(uploadJobs, &job, 0) == pdTRUE) uploadBusy = true;
}

const char *stateName(Visit::Phase value) {
  switch (value) {
    case Visit::Phase::Idle: return "待機";
    case Visit::Phase::Candidate: return "等待已登錄晶片";
    case Visit::Phase::Entry: return "入口活動中";
    case Visit::Phase::Inside: return "推定在內部";
    case Visit::Phase::Exit: return "離開候選";
    case Visit::Phase::Complete: return "結案";
    case Visit::Phase::WaitClear: return "等待入口恢復";
  }
  return "未知";
}
String maskedChip(const String &chip) {
  if (!chip.length()) return "尚未辨識";
  if (chip == "unknown") return chip;
  return "***********" + chip.substring(chip.length() > 4 ? chip.length() - 4 : 0);
}

String debugStatusJson() {
  uint8_t queueHead = 0;
  uint8_t queueCount = 0;
  const bool queueValid = loadQueueMeta(queueHead, queueCount);
  const uint32_t durationSec = visit.active() ? visit.elapsed(millis()) / 1000UL : 0;
  const float averageDistance = currentSession.sample_count == 0
                                    ? 0
                                    : static_cast<float>(distanceSumMm) / currentSession.sample_count;
  String json;
  json.reserve(900);
  json += "{\"state\":\"" + jsonEscape(stateName(visit.phase)) + "\",";
  json += "\"distance_mm\":" + String(latestDistanceMm == UINT16_MAX ? -1 : latestDistanceMm) + ",";
  json += "\"tof_ok\":" + String(latestDistanceMm != UINT16_MAX ? "true" : "false") + ",";
  json += "\"below_entry_threshold\":" + String(latestDistanceMm != UINT16_MAX && latestDistanceMm < Config::ENTRY_THRESHOLD_MM ? "true" : "false") + ",";
  json += "\"rfid_enabled\":" + String(rfidEnabled ? "true" : "false") + ",";
  json += "\"rfid_scanning\":" + String(rfidScanActive ? "true" : "false") + ",";
  json += "\"rfid_status\":\"" + jsonEscape(rfidScanLabel + "：" + rfidLastStatus) + "\",";
  json += "\"rfid_raw\":\"" + jsonEscape(rfidLastRaw) + "\",";
  json += "\"rfid_bytes\":" + String(rfidBytesSeen) + ",";
  json += "\"rfid_invalid_frames\":" + String(rfidInvalidFrames) + ",";
  json += "\"chip_id\":\"" + jsonEscape(maskedChip(currentSession.chip_id)) + "\",";
  json += "\"scan_chip\":\"" + jsonEscape(maskedChip(visit.scanChip)) + "\",";
  json += "\"cat_id\":\"" + jsonEscape(currentSession.cat_id) + "\",";
  json += "\"session_id\":\"" + jsonEscape(currentSession.session_id) + "\",";
  json += "\"duration_sec\":" + String(durationSec) + ",";
  json += "\"min_distance_mm\":" + String(currentSession.sample_count == 0 ? 0 : currentSession.min_distance_mm) + ",";
  json += "\"avg_distance_mm\":" + String(averageDistance, 1) + ",";
  json += "\"sample_count\":" + String(currentSession.sample_count) + ",";
  json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ip\":\"" + jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "") + "\",";
  json += "\"pending_count\":" + String(queueValid ? queueCount : 0) + ",";
  json += "\"last_upload_status\":" + String(lastUploadStatus) + ",";
  json += "\"last_upload_ok\":" + String(lastUploadOk ? "true" : "false") + ",";
  json += "\"last_upload_response\":\"" + jsonEscape(lastUploadResponse) + "\",";
  json += "\"clear_sec\":" + String(visit.clearElapsed(millis()) / 1000UL) + ",";
  json += "\"identity_conflict\":" + String(visit.conflict ? "true" : "false") + ",";
  json += "\"recent_reason\":\"" + jsonEscape(recentReason) + "\",";
  json += "\"recent_state\":\"" + jsonEscape(recentState) + "\",";
  json += "\"failed_records\":" + String(failedRecords) + ",";
  json += "\"storage_errors\":" + String(storageErrors) + ",";
  json += "\"interrupted_pending\":" + String(prefs.isKey("interrupted") || !journalWritable ? "true" : "false") + ",";
  json += "\"max_range_gap_ms\":" + String(maxRangeGapMs) + "}";
  return json;
}

static const char DEBUG_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>貓砂櫃即時除錯</title><style>
body{font-family:system-ui,sans-serif;background:#111827;color:#f9fafb;margin:0;padding:16px}
h1{font-size:1.35rem}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:12px}
.card{background:#1f2937;border-radius:12px;padding:14px}.label{color:#9ca3af;font-size:.85rem}
.value{font-size:1.25rem;font-weight:700;overflow-wrap:anywhere}.ok{color:#34d399}.warn{color:#fbbf24}
pre{white-space:pre-wrap;overflow-wrap:anywhere;margin:.4rem 0 0}small{color:#9ca3af}
</style></head><body><h1>貓砂櫃即時除錯</h1><div class="grid">
<section class="card"><div class="label">系統狀態</div><div id="state" class="value">--</div><small id="network">--</small></section>
<section class="card"><div class="label">VL53L0X 距離</div><div id="distance" class="value">--</div><small id="tof">--</small></section>
<section class="card"><div class="label">RFID</div><div id="rfid" class="value">--</div><small id="rfidStats">--</small></section>
<section class="card"><div class="label">晶片編號／貓咪</div><div id="identity" class="value">--</div></section>
<section class="card"><div class="label">UART 結果（遮罩）</div><pre id="raw">尚未收到資料</pre></section>
<section class="card"><div class="label">目前紀錄</div><div id="session" class="value">--</div><small id="metrics">--</small></section>
<section class="card"><div class="label">上傳</div><div id="upload" class="value">--</div><small id="pending">--</small></section>
<section class="card"><div class="label">事件收尾與儲存</div><div id="closure"></div></section>
</div><script>
const show=(id,text)=>document.getElementById(id).textContent=text;
async function update(){try{const r=await fetch('/api/status',{cache:'no-store'});const d=await r.json();
show('state',d.state+' · 清空 '+d.clear_sec+' 秒'+(d.identity_conflict?' · 身分衝突':''));show('network',d.wifi_connected?'Wi-Fi 已連線 · '+d.ip:'Wi-Fi 未連線');
show('distance',d.tof_ok?d.distance_mm+' mm':'讀取逾時');show('tof',d.below_entry_threshold?'小於 200mm：已達進入門檻':'等待小於 200mm');
show('rfid',(d.rfid_enabled?'已開啟':'已關閉')+' · '+d.rfid_status);show('rfidStats','UART 位元組 '+d.rfid_bytes+' · 無效封包 '+d.rfid_invalid_frames);
show('identity',(d.chip_id||'--')+' ／ '+(d.cat_id||'--'));show('raw','本輪：'+d.scan_chip+' · '+(d.rfid_raw||'尚未收到資料'));
show('session',d.session_id||'尚無紀錄');show('metrics','停留 '+d.duration_sec+'秒 · 最短 '+d.min_distance_mm+'mm · 平均 '+d.avg_distance_mm+'mm · '+d.sample_count+'次');
show('upload',d.last_upload_ok?'最近上傳成功':(d.last_upload_status?'最近上傳失敗：'+d.last_upload_status:'尚未上傳'));show('pending','待上傳 '+d.pending_count+' 筆'+(d.last_upload_response?' · '+d.last_upload_response:''));
show('closure',d.recent_reason+' · '+d.recent_state+' · 未保存 '+d.failed_records+' 筆 · 儲存錯誤 '+d.storage_errors+' · 最大量測間隔 '+d.max_range_gap_ms+'ms'+(d.interrupted_pending?' · 有本機中斷紀錄待查':''));
}catch(e){show('network','無法連線至裝置');}}update();setInterval(update,250);
</script></body></html>)rawliteral";

void serviceDebugServer() {
  if (debugServerStarted) debugServer.handleClient();
}

void startDebugServer() {
  if (!DEBUG_WEB_SERVER || strlen(WIFI_SSID) == 0 || strlen(WIFI_PASSWORD) == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;

  debugServer.on("/", HTTP_GET, []() { debugServer.send_P(200, "text/html; charset=utf-8", DEBUG_HTML); });
  debugServer.on("/api/status", HTTP_GET, []() {
    debugServer.sendHeader("Cache-Control", "no-store");
    debugServer.send(200, "application/json; charset=utf-8", debugStatusJson());
  });
  debugServer.onNotFound([]() { debugServer.send(404, "text/plain; charset=utf-8", "找不到頁面"); });
  debugServer.begin();
  debugServerStarted = true;
  Serial.printf("DEBUG web: http://%s\n", WiFi.localIP().toString().c_str());
}

void handleInvalidDistance() {
  if (invalidDistanceSinceMs == 0) invalidDistanceSinceMs = millis();
  const uint32_t now = millis();
  if (now - invalidDistanceSinceMs >= Config::TOF_INVALID_RESTART_MS &&
      now - lastTofInitAttemptMs >= Config::TOF_REINIT_BACKOFF_MS) {
    Serial.println("VL53L0X invalid; attempting reinitialization");
    if (initToF()) {
      Serial.println("VL53L0X reinitialized");
      invalidDistanceSinceMs = 0;
    } else {
      Serial.println("VL53L0X reinitialization failed; backing off");
    }
  }
}

void beginSession(uint16_t distance) {
  resetSession();
  currentSession.session_id = createSessionId();
  sessionStartMs = visit.started;
  eventStartUtc = candidateStartUtc;
  candidateStartUtc = 0;
  candidateFirstDistance = UINT16_MAX;
  addDistanceSample(distance);
}

void finishSession() {
  SessionRecord completed = currentSession;
  completed.duration_sec = max(1UL, visit.durationMs() / 1000UL);
  completed.avg_distance_mm = completed.sample_count
    ? static_cast<float>(distanceSumMm) / completed.sample_count : 0;
  if (eventStartUtc) {
    completed.enter_time = formatIsoUtc(eventStartUtc);
    completed.exit_time = formatIsoUtc(eventStartUtc + completed.duration_sec);
  }
  recentSessionId = completed.session_id;
  recentReason = visit.conflict ? "identity_conflict" : Visit::reasonName(visit.reason);
  if (visit.conflict) {
    recentState = "身分衝突，捨棄且不上傳";
  } else if (!isRegisteredChip(completed.chip_id)) {
    recentReason = "missing_registered_identity";
    recentState = "缺少已登錄身分，捨棄且不上傳";
  } else if (enqueuePending(completed)) {
    recentState = "已保存，待上傳";
  } else {
    ++failedRecords;
    storageFailure("事件未保存：佇列滿或儲存失敗");
  }
  // Reboot never replays this journal, even when remove fails.
  if (journalWritable && prefs.isKey("active") && !prefs.remove("active")) {
    journalWritable = false; storageFailure("結案備份清除失敗，停用活動備份");
  }
  Serial.printf("EVENT reason=%s saved_state=%s lost=%lu\n", recentReason.c_str(), recentState.c_str(),
    static_cast<unsigned long>(failedRecords));
  rfidOff();
  visit.release();
  resetSession();
}

void processStateMachine() {
  const uint32_t now = millis();
  if (!ranged || uint32_t(now - lastRangeMs) >= Config::ACTIVE_RANGING_PERIOD_MS) {
    if (ranged) maxRangeGapMs = max(maxRangeGapMs, uint32_t(now - lastRangeMs));
    lastRangeMs = now; ranged = true;
    const uint16_t d = readDistanceMm();
    const Visit::Phase previousPhase = visit.phase;
    visit.sample(millis(), d != UINT16_MAX, d);
    if (previousPhase == Visit::Phase::Idle && visit.candidate()) {
      candidateStartUtc = timeIsValid() ? time(nullptr) : 0;
      candidateFirstDistance = d;
    } else if (previousPhase == Visit::Phase::Candidate && !visit.candidate()) {
      candidateStartUtc = 0;
      candidateFirstDistance = UINT16_MAX;
    } else if (visit.active()) addDistanceSample(d);
    if (d == UINT16_MAX) handleInvalidDistance();
    else invalidDistanceSinceMs = 0;
  }
  const bool wasCandidate = visit.candidate();
  visit.tick(millis());
  if (wasCandidate && !visit.candidate()) {
    candidateStartUtc = 0;
    candidateFirstDistance = UINT16_MAX;
  }
  syncRfidScan();
  pollRfidScan();
  if (visit.phase == Visit::Phase::Complete) finishSession();
  if (visit.active() && uint32_t(millis() - lastCheckpointMs) >= Config::SESSION_CHECKPOINT_MS)
    checkpointActiveSession();
}

void setup() {
  Serial.begin(115200);
  pinMode(Config::RFID_ENABLE_PIN, OUTPUT);
  rfidOff();
  pinMode(Config::TOF_INT_PIN, INPUT_PULLUP);
  WiFi.mode(WIFI_OFF);
  rfidSerial.begin(9600, SERIAL_8N1, Config::RFID_RX_PIN, Config::RFID_TX_PIN);
  if (!prefs.begin("litter", false)) {
    journalWritable = false;
    storageFailure("NVS 開啟失敗，無法確認舊紀錄");
  }
  if (!initToF()) {
    Serial.println("VL53L0X not found; idle recovery will retry with backoff");
  }
  if (journalWritable) recoverJournal();
  visit.phase = prefs.isKey("interrupted") || !journalWritable ? Visit::Phase::WaitClear : Visit::Phase::Idle;
  uploadJobs = xQueueCreate(1, sizeof(UploadJob));
  uploadResults = xQueueCreate(1, sizeof(UploadResult));
  if (!uploadJobs || !uploadResults ||
      xTaskCreatePinnedToCore(uploadWorker, "upload", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
    storageFailure("背景上傳工作建立失敗");
    if (uploadJobs) vQueueDelete(uploadJobs);
    if (uploadResults) vQueueDelete(uploadResults);
    uploadJobs = nullptr; uploadResults = nullptr;
  }
  if (DEBUG_WEB_SERVER && strlen(WIFI_SSID)) {
    WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    configTzTime("UTC0", "time.google.com", "pool.ntp.org");
  }

  Serial.println("Smart litter monitor ready");
  startDebugServer();
}

void loop() {
  serviceDebugServer();
  processStateMachine();
  serviceUploads();
  if (!debugServerStarted) startDebugServer();
  serviceDebugServer();
  static uint32_t lastReport = 0;
  if (uint32_t(millis() - lastReport) >= 1000) {
    lastReport = millis();
    Serial.printf("STATE=%s elapsed=%lu clear=%lu scan=%s result=%s retained=%s cat=%s conflict=%u closure=%s storage=%s lost=%lu errors=%lu\n",
      stateName(visit.phase), static_cast<unsigned long>(visit.active() ? visit.elapsed(millis()) : 0),
      static_cast<unsigned long>(visit.clearElapsed(millis())), rfidScanLabel.c_str(),
      rfidLastStatus.c_str(), maskedChip(currentSession.chip_id).c_str(), currentSession.cat_id.c_str(),
      visit.conflict, recentReason.c_str(), recentState.c_str(),
      static_cast<unsigned long>(failedRecords), static_cast<unsigned long>(storageErrors));
    Serial.printf("SCAN scanning=%u chip=%s upload=%d ok=%u\n", visit.scanning,
      maskedChip(visit.scanChip).c_str(), lastUploadStatus, lastUploadOk);
  }
  delay(1);
}
