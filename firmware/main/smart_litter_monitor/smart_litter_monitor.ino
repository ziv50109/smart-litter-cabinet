#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <VL53L0X.h>
#include "esp_sleep.h"
#include "esp_system.h"
#include <time.h>
#include <stdlib.h>
#include "app_config.h"
#include "certificates.h"

// Keep secrets.h ONLY on the local computer used to compile/flash the board.
// Do not add it to Cirkit or any shared/cloud project.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

enum class State {
  IDLE,
  CANDIDATE_ENTRY,
  IDENTIFYING,
  OCCUPIED,
  CANDIDATE_EXIT,
  UPLOAD
};

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
State state = State::IDLE;
SessionRecord currentSession;
uint32_t stateSinceMs = 0;
uint32_t sessionStartMs = 0;
uint64_t distanceSumMm = 0;
uint32_t invalidDistanceSinceMs = 0;
uint32_t lastCheckpointMs = 0;
uint32_t lastTofInitAttemptMs = 0;
bool tofInitialized = false;

uint32_t fnv1a32(const String &text) {
  uint32_t hash = 0x811C9DC5;
  for (size_t i = 0; i < text.length(); ++i) {
    hash ^= static_cast<uint8_t>(text[i]);
    hash *= 0x01000193;
  }
  return hash;
}

String catNameForChip(const String &chipId) {
  const uint32_t hash = fnv1a32(chipId);
  if (CAT_1_CHIP_HASH != 0UL && hash == CAT_1_CHIP_HASH) return CAT_1_NAME;
  if (CAT_2_CHIP_HASH != 0UL && hash == CAT_2_CHIP_HASH) return CAT_2_NAME;
  return "unknown";
}

void rfidOff() {
  digitalWrite(Config::RFID_ENABLE_PIN, LOW);
}

void rfidOn() {
  while (rfidSerial.available()) rfidSerial.read();
  digitalWrite(Config::RFID_ENABLE_PIN, HIGH);
  delay(120);
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

String readChipId(uint32_t timeoutMs) {
  rfidOn();
  const uint32_t deadline = millis() + timeoutMs;
  String frame;
  bool receiving = false;
  while (static_cast<int32_t>(deadline - millis()) > 0) {
    while (rfidSerial.available()) {
      const char c = static_cast<char>(rfidSerial.read());
      if (c == '$') {
        frame = "";
        receiving = true;
      } else if (c == '#' && receiving) {
        String chipId;
        if (parseRfidFrame(frame, chipId)) {
          rfidOff();
          return chipId;
        }
        receiving = false;
      } else if (receiving) {
        if (frame.length() < 24) frame += c;
        else receiving = false;
      }
    }
    delay(2);
  }
  rfidOff();
  return "";
}

bool initToF() {
  lastTofInitAttemptMs = millis();
  Wire.begin(Config::TOF_SDA_PIN, Config::TOF_SCL_PIN);
  Wire.setClock(400000);
  tof.setTimeout(100);
  tofInitialized = tof.init();
  if (!tofInitialized) return false;
  tof.setMeasurementTimingBudget(Config::TOF_TIMING_BUDGET_US);
  return true;
}

uint16_t readDistanceMm() {
  if (!tofInitialized) return UINT16_MAX;
  const uint16_t distance = tof.readRangeSingleMillimeters();
  if (tof.timeoutOccurred() || distance == 0 || distance > 2000) return UINT16_MAX;
  return distance;
}

void idleLowPowerWait() {
  WiFi.mode(WIFI_OFF);
  rfidOff();
  const uint64_t sleepUs = static_cast<uint64_t>(Config::IDLE_RANGING_PERIOD_MS) * 1000ULL;
  esp_sleep_enable_timer_wakeup(sleepUs);
  esp_light_sleep_start();
}

void resetSession() {
  currentSession = SessionRecord{};
  distanceSumMm = 0;
  sessionStartMs = 0;
  lastCheckpointMs = 0;
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
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < Config::WIFI_TIMEOUT_MS) {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) return false;

  if (!timeIsValid()) {
    configTzTime("UTC0", "time.google.com", "pool.ntp.org");
    const uint32_t ntpStarted = millis();
    while (!timeIsValid() && millis() - ntpStarted < Config::NTP_TIMEOUT_MS) delay(100);
  }
  return true;
}

void disconnectWifi() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

void ensureTimestampsIfTimeValid(SessionRecord &record) {
  if (record.enter_time.length() && record.exit_time.length()) return;
  if (!timeIsValid()) return;
  const time_t exitEpoch = time(nullptr);
  const time_t enterEpoch = exitEpoch - static_cast<time_t>(record.duration_sec);
  record.enter_time = formatIsoUtc(enterEpoch);
  record.exit_time = formatIsoUtc(exitEpoch);
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

bool uploadRecord(SessionRecord &record, bool allowTimestampBackfill) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (allowTimestampBackfill) ensureTimestampsIfTimeValid(record);
  WiFiClientSecure client;
  client.setCACert(GTS_ROOT_R1);
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(client, APP_SCRIPT_URL)) return false;
  http.addHeader("Content-Type", "application/json");
  const int status = http.POST(recordJson(record));
  const String response = status > 0 ? http.getString() : "";
  http.end();
  const bool ok = status >= 200 && status < 300 && response.indexOf("\"ok\":true") >= 0;
  Serial.printf("UPLOAD status=%d ok=%s\n", status, ok ? "true" : "false");
  return ok;
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
    Serial.println("PENDING queue metadata invalid; resetting queue metadata");
    head = 0;
    count = 0;
    return storeQueueMeta(head, count);
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
    Serial.println("PENDING queue full; record remains in RAM until restart");
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
  // fails before this write, the active-session checkpoint still owns record.
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

void flushPending() {
  while (true) {
    uint8_t head = 0;
    uint8_t count = 0;
    if (!loadQueueMeta(head, count) || !count) break;
    SessionRecord pending;
    if (!decodeRecord(prefs.getString(queueKey(head).c_str(), ""), pending)) {
      Serial.println("PENDING record invalid; discarding corrupt slot");
      if (!removeOldestPending()) break;
      continue;
    }
    // Pending records may intentionally have blank timestamps. Never turn
    // their retry time into an apparent event time.
    if (!uploadRecord(pending, false)) break;
    if (!removeOldestPending()) break;
  }
}

void transitionTo(State next, bool resetInvalidDistance = true) {
  state = next;
  stateSinceMs = millis();
  if (resetInvalidDistance) invalidDistanceSinceMs = 0;
}

bool checkpointActiveSession() {
  if (!currentSession.session_id.length() || currentSession.sample_count == 0) return true;
  SessionRecord snapshot = currentSession;
  snapshot.duration_sec = max(1UL, (millis() - sessionStartMs) / 1000UL);
  snapshot.avg_distance_mm = static_cast<float>(distanceSumMm) / snapshot.sample_count;
  const String encoded = encodeRecord(snapshot);
  if (prefs.putString("active", encoded) != encoded.length()) {
    Serial.println("ACTIVE checkpoint write failed");
    return false;
  }
  lastCheckpointMs = millis();
  return true;
}

bool restoreActiveSession() {
  const String encoded = prefs.getString("active", "");
  SessionRecord restored;
  if (!encoded.length() || !decodeRecord(encoded, restored)) {
    if (encoded.length()) prefs.remove("active");
    return false;
  }
  currentSession = restored;
  const uint32_t maxRestoredSeconds = Config::MAX_SESSION_DURATION_MS / 1000UL;
  const uint32_t restoredSeconds = restored.duration_sec > maxRestoredSeconds
                                       ? maxRestoredSeconds
                                       : restored.duration_sec;
  sessionStartMs = millis() - restoredSeconds * 1000UL;
  distanceSumMm = static_cast<uint64_t>(restored.avg_distance_mm * restored.sample_count);
  lastCheckpointMs = millis();
  Serial.println("Recovered an interrupted active session from NVS");
  return true;
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

bool sessionNeedsFailSafeClose() {
  const uint32_t now = millis();
  return (sessionStartMs != 0 && now - sessionStartMs >= Config::MAX_SESSION_DURATION_MS) ||
         (invalidDistanceSinceMs != 0 &&
          now - invalidDistanceSinceMs >= Config::MAX_INVALID_OCCUPIED_MS);
}

void beginSession(uint16_t distance) {
  resetSession();
  currentSession.session_id = createSessionId();
  sessionStartMs = millis();
  addDistanceSample(distance);
  const String chip = readChipId(Config::RFID_TIMEOUT_MS);
  currentSession.chip_id = chip.length() ? chip : "unknown";
  currentSession.cat_id = chip.length() ? catNameForChip(chip) : "unknown";
  Serial.printf("RFID cat=%s read=%s\n", currentSession.cat_id.c_str(), chip.length() ? "yes" : "no");
  checkpointActiveSession();
}

void finalizeSession() {
  currentSession.duration_sec = (millis() - sessionStartMs) / 1000UL;
  if (currentSession.duration_sec == 0) currentSession.duration_sec = 1;
  if (currentSession.sample_count == 0) {
    currentSession.min_distance_mm = 2000;
    currentSession.avg_distance_mm = 2000;
  } else {
    currentSession.avg_distance_mm = static_cast<float>(distanceSumMm) / currentSession.sample_count;
  }
}

void processStateMachine() {
  const uint32_t now = millis();
  switch (state) {
    case State::IDLE: {
      idleLowPowerWait();
      const uint16_t d = readDistanceMm();
      if (d == UINT16_MAX) {
        handleInvalidDistance();
      } else {
        invalidDistanceSinceMs = 0;
        if (d <= Config::ENTRY_THRESHOLD_MM) transitionTo(State::CANDIDATE_ENTRY);
      }
      break;
    }
    case State::CANDIDATE_ENTRY: {
      const uint16_t d = readDistanceMm();
      if (d == UINT16_MAX) {
        handleInvalidDistance();
        transitionTo(State::IDLE, false);
      } else if (d >= Config::EXIT_THRESHOLD_MM) {
        invalidDistanceSinceMs = 0;
        transitionTo(State::IDLE);
      }
      else if (now - stateSinceMs >= Config::ENTRY_DEBOUNCE_MS) {
        transitionTo(State::IDENTIFYING);
        beginSession(d);
        // RFID reading is synchronous. Check distance immediately afterwards
        // so an animal that already left does not become a normal occupied run.
        const uint16_t afterRfid = readDistanceMm();
        if (afterRfid == UINT16_MAX) {
          transitionTo(State::OCCUPIED);
          invalidDistanceSinceMs = millis();
        } else {
          invalidDistanceSinceMs = 0;
          addDistanceSample(afterRfid);
          transitionTo(afterRfid >= Config::EXIT_THRESHOLD_MM ? State::CANDIDATE_EXIT
                                                               : State::OCCUPIED);
        }
      }
      delay(Config::ACTIVE_RANGING_PERIOD_MS);
      break;
    }
    case State::IDENTIFYING:
      // Identification is executed synchronously by beginSession().
      transitionTo(State::OCCUPIED);
      break;
    case State::OCCUPIED: {
      const uint16_t d = readDistanceMm();
      if (d == UINT16_MAX) handleInvalidDistance();
      else {
        invalidDistanceSinceMs = 0;
        addDistanceSample(d);
        if (d >= Config::EXIT_THRESHOLD_MM) transitionTo(State::CANDIDATE_EXIT);
      }
      if (sessionNeedsFailSafeClose()) {
        Serial.println("Closing session after sensor/session fail-safe limit");
        finalizeSession();
        checkpointActiveSession();
        transitionTo(State::UPLOAD);
        break;
      }
      if (millis() - lastCheckpointMs >= Config::SESSION_CHECKPOINT_MS) checkpointActiveSession();
      delay(Config::ACTIVE_RANGING_PERIOD_MS);
      break;
    }
    case State::CANDIDATE_EXIT: {
      const uint16_t d = readDistanceMm();
      if (d == UINT16_MAX) {
        handleInvalidDistance();
      } else if (d <= Config::ENTRY_THRESHOLD_MM) {
        invalidDistanceSinceMs = 0;
        addDistanceSample(d);
        transitionTo(State::OCCUPIED);
      } else if (now - stateSinceMs >= Config::EXIT_DEBOUNCE_MS) {
        finalizeSession();
        checkpointActiveSession();
        transitionTo(State::UPLOAD);
      }
      if (state == State::CANDIDATE_EXIT && sessionNeedsFailSafeClose()) {
        Serial.println("Closing session after sensor/session fail-safe limit");
        finalizeSession();
        checkpointActiveSession();
        transitionTo(State::UPLOAD);
        break;
      }
      delay(Config::ACTIVE_RANGING_PERIOD_MS);
      break;
    }
    case State::UPLOAD: {
      const bool online = connectWifiAndTryTime();
      // Timestamp the current event as soon as this upload cycle obtains a
      // valid clock. Older queued events intentionally remain untouched.
      if (online && currentSession.session_id.length()) ensureTimestampsIfTimeValid(currentSession);
      if (online) flushPending();
      bool currentSafe = currentSession.session_id.length() == 0;
      if (!currentSafe) {
        currentSafe = online && uploadRecord(currentSession, true);
        if (!currentSafe) currentSafe = enqueuePending(currentSession);
      }
      disconnectWifi();
      if (!currentSafe) {
        // Queue is full. Keep the record and its NVS checkpoint, then retry later.
        esp_sleep_enable_timer_wakeup(60000000ULL);
        esp_light_sleep_start();
        break;
      }
      if (!prefs.remove("active")) Serial.println("ACTIVE checkpoint remove failed; duplicate retry is safe");
      resetSession();
      transitionTo(State::IDLE);
      break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(Config::RFID_ENABLE_PIN, OUTPUT);
  rfidOff();
  pinMode(Config::TOF_INT_PIN, INPUT_PULLUP);
  WiFi.mode(WIFI_OFF);
  rfidSerial.begin(9600, SERIAL_8N1, Config::RFID_RX_PIN, Config::RFID_TX_PIN);
  prefs.begin("litter", false);
  if (!initToF()) {
    Serial.println("VL53L0X not found; idle recovery will retry with backoff");
  }
  uint8_t queueHead = 0;
  uint8_t queueCount = 0;
  const bool queueReady = loadQueueMeta(queueHead, queueCount);
  if (restoreActiveSession()) transitionTo(State::OCCUPIED);
  else if (queueReady && queueCount > 0) transitionTo(State::UPLOAD);
  else transitionTo(State::IDLE);
  Serial.println("Smart litter monitor ready");
}

void loop() {
  processStateMachine();
}
