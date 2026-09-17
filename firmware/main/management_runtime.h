#pragma once
// Included after storage/record helpers. Network task owns Wi-Fi, WebServer and OTA.

bool adminConfigured() {
  return strlen(WEB_ADMIN_USER) > 0 && strlen(WEB_ADMIN_PASSWORD) >= 8;
}
String csrfToken;
uint32_t nonceIssuedAt = 0;
bool nonceIssued = false;
struct FailedPeer { uint32_t ip = 0, last = 0; uint8_t failures = 0; bool used = false; };
FailedPeer failedPeers[4];
Runtime::OtaTransfer otaTransfer;
bool otaRequestOpen = false, otaBegun = false, otaResponseSent = false;
uint16_t otaHttpError = 400;
uint8_t otaHeader[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)];
size_t otaHeaderSize = 0;

String randomToken() {
  char b[33];
  snprintf(b, sizeof(b), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(),
    (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long)esp_random());
  return String(b);
}
void privateHeaders() {
  web.sendHeader("Cache-Control", "no-store");
  web.sendHeader("X-Content-Type-Options", "nosniff");
  web.sendHeader("X-Frame-Options", "DENY");
  web.sendHeader("Referrer-Policy", "no-referrer");
}
FailedPeer &peerFailures(uint32_t now) {
  const uint32_t ip = uint32_t(web.client().remoteIP());
  size_t selected = 0;
  for (size_t i = 0; i < 4; ++i) {
    if (failedPeers[i].used && failedPeers[i].ip == ip) return failedPeers[i];
    if (!failedPeers[i].used) { selected = i; break; }
    if (uint32_t(now - failedPeers[i].last) > uint32_t(now - failedPeers[selected].last)) selected = i;
  }
  failedPeers[selected] = FailedPeer{};
  failedPeers[selected].used = true; failedPeers[selected].ip = ip;
  return failedPeers[selected];
}
bool requireAdmin() {
  privateHeaders();
  if (!adminConfigured()) { web.send(503, "text/plain", "management disabled"); return false; }
  const uint32_t now = millis();
  FailedPeer &peer = peerFailures(now);
  if (peer.failures >= Config::ADMIN_FAILURE_LIMIT && uint32_t(now - peer.last) < Config::ADMIN_RETRY_COOLDOWN_MS) {
    web.sendHeader("Retry-After", String((Config::ADMIN_RETRY_COOLDOWN_MS + 999) / 1000));
    web.send(429, "text/plain", "retry later"); return false;
  }
  if (uint32_t(now - peer.last) >= Config::ADMIN_RETRY_COOLDOWN_MS) peer.failures = 0;
  const String authorization = web.header("Authorization");
  const bool fresh = nonceIssued && uint32_t(now - nonceIssuedAt) < Config::ADMIN_NONCE_LIFETIME_MS;
  // authenticate() accepts Basic as well: explicitly refuse downgrade to Basic.
  if (fresh && authorization.startsWith("Digest ") && web.authenticate(WEB_ADMIN_USER, WEB_ADMIN_PASSWORD)) {
    peer.failures = 0; return true;
  }
  if (authorization.length()) { ++peer.failures; peer.last = now; }
  nonceIssuedAt = now; nonceIssued = true;
  web.requestAuthentication(DIGEST_AUTH, "LitterCabinet", "Login required");
  return false;
}
bool validMutation() {
  const String host = web.header("Host");
  const String address = WiFi.localIP().toString();
  if (host != address && host != address + ":80") return false;
  if (web.header("Origin") != "http://" + host) return false;
  const String provided = web.header("X-CSRF-Token");
  return csrfToken.length() == 32 && provided.length() == csrfToken.length() && provided.equalsConstantTime(csrfToken);
}
bool requireMutation() {
  if (!requireAdmin()) return false;
  if (!validMutation()) { web.send(403, "text/plain", "invalid request origin or token"); return false; }
  return true;
}
const char *phaseName(Visit::Phase phase) {
  switch (phase) {
    case Visit::Phase::Idle: return "idle";
    case Visit::Phase::Candidate: return "candidate";
    case Visit::Phase::Entry: return "entry";
    case Visit::Phase::Exit: return "exit";
    case Visit::Phase::Complete: return "complete";
    case Visit::Phase::WaitClear: return "wait_clear";
  }
  return "unknown";
}
String statusJson() {
  String j;
  {
    ScopedLock guard(sensorMutex);
    j = "{\"firmware\":\"" + String(FW_VERSION) + "\",\"state\":\"" + phaseName(visit.phase) + "\",";
    j += "\"distance_mm\":" + String(latestDistance == UINT16_MAX ? -1 : int(latestDistance));
    j += ",\"rfid_on\":" + String(rfidPower ? "true" : "false");
    j += ",\"global_max_gap_ms\":" + String(globalMaxGap);
    j += ",\"diagnostics_dropped\":" + String(diagnosticsDropped);
  }
  j += ",\"pending\":" + String(pendingCount());
  j += ",\"network\":\"" + String(netMode.load() == NetMode::Sta ? "sta" : "off") + "\"";
  j += ",\"requested_mode\":\"" + String(LITTER_CONNECTED_STANDBY ? "connected_standby" : "offline") + "\"";
  j += ",\"effective_mode\":\"" + String(connectedStandby.load() ? "connected_standby" : "offline") + "\"";
  j += ",\"automatic_pm_configured\":" + String(automaticPm.load() ? "true" : "false");
  j += ",\"power_init\":\"" + String(powerReason.load()) + "\"";
  j += ",\"ota_paused\":" + String(otaPaused.load() ? "true" : "false");
  j += ",\"arduino_core\":\"" + String(ESP_ARDUINO_VERSION_STR) + "\",\"idf\":\"" + String(esp_get_idf_version()) + "\"}";
  return j;
}
String liveLogText() {
  // Snapshot bounded log text; no log lock is held during network transmission.
  String out; out.reserve(8000);
  ScopedLock guard(logMutex);
  const uint32_t total = min(liveSeq, uint32_t(LIVE_LOG_CAP));
  const uint32_t first = liveSeq > LIVE_LOG_CAP ? liveSeq - LIVE_LOG_CAP : 0;
  for (uint32_t i = 0; i < total; ++i) {
    const LiveLog &r = liveLogs[(first + i) % LIVE_LOG_CAP];
    out += String(r.ms) + "\t" + r.action + "\t" + r.detail + "\n";
  }
  return out;
}
String traceSessionId(File &file) {
  char line[96]; size_t used = 0;
  while (file.available() && used + 1 < sizeof(line)) {
    const int c = file.read();
    if (c == '\n') break;
    if (c != '\r') line[used++] = char(c);
  }
  line[used] = 0;
  const String prefix = "session_id,";
  if (strncmp(line, prefix.c_str(), prefix.length()) != 0) return "";
  const char *id = line + prefix.length();
  return Runtime::safeSessionId(id) ? String(id) : String("");
}
String diagIndexHtml() {
  String h = "<!doctype html><html lang='zh-Hant'><meta charset='utf-8'><meta name='viewport' content='width=device-width'><h1>診斷資料</h1><p><a href='/diag/current'>下載目前紀錄（JSONL）</a> · <a href='/diag/previous'>下載封存紀錄（JSONL）</a></p><ul>";
  ScopedLock guard(diagMutex);
  const uint8_t next = diagPrefs.getUChar("slot", 0) % Config::TRACE_SLOTS;
  for (uint8_t i = 0; i < Config::TRACE_SLOTS; ++i) {
    const uint8_t slot = (next + Config::TRACE_SLOTS - 1 - i) % Config::TRACE_SLOTS;
    File f = SPIFFS.open("/trace" + String(slot) + ".csv", FILE_READ);
    if (!f) continue;
    const String id = traceSessionId(f); f.close();
    if (id.length()) h += "<li><a href='/trace?slot=" + String(slot) + "&amp;sid=" + id + "'>" + id + "（CSV）</a></li>";
  }
  return h + "</ul><p><a href='/'>返回</a></p></html>";
}
void sendFilePath(const String &path, const char *mime, String filename, const String &expectedId = "") {
  std::unique_ptr<uint8_t[]> bytes;
  size_t size = 0;
  {
    ScopedLock guard(diagMutex);
    if (!spiffsReady) { web.send(503, "text/plain", "diagnostics unavailable"); return; }
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) { web.send(404, "text/plain", "not found"); return; }
    if (path.startsWith("/trace")) {
      const String id = traceSessionId(f);
      if (!id.length()) { f.close(); web.send(500, "text/plain", "invalid trace metadata"); return; }
      if (expectedId.length() && expectedId != id) {
        f.close(); web.send(409, "text/plain; charset=utf-8", "紀錄已被輪替，請重新整理列表"); return;
      }
      filename = id + ".csv";
      f.seek(0);
    }
    size = f.size();
    if (size > Config::MAX_DOWNLOAD_BYTES) { f.close(); web.send(413, "text/plain", "diagnostic too large"); return; }
    bytes.reset(new (std::nothrow) uint8_t[size ? size : 1]);
    if (!bytes) { f.close(); web.send(503, "text/plain", "not enough memory for snapshot"); return; }
    const size_t read = f.read(bytes.get(), size); f.close();
    if (read != size) { web.send(500, "text/plain", "snapshot read failed"); return; }
  }
  // Immutable snapshot: a slow client cannot hold storage locks or mix two sessions.
  web.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  web.setContentLength(size);
  web.send(200, mime, "");
  if (size) web.sendContent(reinterpret_cast<const char *>(bytes.get()), size);
}
String mainHtml() {
  String h = R"HTML(<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><style>body{font-family:system-ui;margin:20px;max-width:760px}pre{white-space:pre-wrap}button,input{font-size:16px;padding:10px;margin:6px}</style><h1>Smart Litter Cabinet</h1><pre id="status">讀取中…</pre><p><a href="/diagnostics">診斷資料</a> · <a href="/live">即時 Log</a></p><h2>Web OTA</h2><p>更新期間會暫停偵測，請先確認貓咪未使用。只選應用程式 .bin，不要選 merged、bootloader 或 partitions 映像。</p><input id="firmware" type="file" accept=".bin,application/octet-stream"><button id="update">上傳並更新</button><p><button id="normal">結束管理，回待機</button></p><pre id="result"></pre><script>
)HTML";
  h += "const csrf='" + csrfToken + "';\n";
  h += R"HTML(let uploading=false, stopped=false;
const result=document.getElementById('result'), status=document.getElementById('status');
async function poll(){if(!document.hidden&&!uploading&&!stopped){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);status.textContent=JSON.stringify(await r.json(),null,2)}catch(e){status.textContent=e.message}}setTimeout(poll,5000)}
document.getElementById('update').onclick=async()=>{const file=document.getElementById('firmware').files[0];if(!file)return;uploading=true;result.textContent='上傳中，請勿斷電';try{const body=new FormData();body.append('firmware',file);const r=await fetch('/update',{method:'POST',headers:{'X-CSRF-Token':csrf,'X-Firmware-Size':String(file.size)},body});const text=await r.text();result.textContent=text;if(!r.ok)throw Error(text);stopped=true}catch(e){result.textContent=e.message}finally{uploading=false}};
document.getElementById('normal').onclick=async()=>{try{const r=await fetch('/normal',{method:'POST',headers:{'X-CSRF-Token':csrf}});result.textContent=await r.text();if(r.ok)stopped=true}catch(e){result.textContent=e.message}};poll();
</script></html>)HTML";
  return h;
}

bool reserveOta() {
  ScopedLock guard(sensorMutex, pdMS_TO_TICKS(Config::OTA_FRESH_SAMPLE_MS));
  if (!guard.held) return false;
  const uint32_t now = millis();
  const bool safe = !metrics.open && !rfidPower && !otaPaused.load() &&
    Runtime::canPauseForOta(visit.phase == Visit::Phase::Idle, visit.previousValid, visit.previousBlocked,
      now, visit.lastSample, visit.currentClearMs(now), Config::OTA_FRESH_SAMPLE_MS, Config::OTA_CLEAR_MS);
  if (safe) otaPaused.store(true);
  return safe;
}
void cleanupOta() {
  if (otaBegun) Update.abort();
  otaBegun = otaRequestOpen = otaResponseSent = false;
  otaHeaderSize = 0; otaTransfer.reset(); otaPaused.store(false);
}
void failOta(uint16_t status) {
  otaTransfer.failed = true; otaHttpError = status;
  if (otaBegun) { Update.abort(); otaBegun = false; }
  otaPaused.store(false);
}
void uploadFirmwareChunk() {
  HTTPUpload &u = web.upload();
  if (u.status == UPLOAD_FILE_START) {
    if (otaRequestOpen) { failOta(400); return; }
    otaRequestOpen = true; otaResponseSent = false; otaHeaderSize = 0;
    if (!requireMutation()) {
      otaResponseSent = true; failOta(403); web.client().stop(); return;
    }
    uint32_t size = 0, requestSize = 0;
    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (u.name != "firmware" || !u.filename.endsWith(".bin") || !partition ||
        !Runtime::decimal(web.header("X-Firmware-Size").c_str(), size) ||
        !Runtime::decimal(web.header("Content-Length").c_str(), requestSize) ||
        size < sizeof(otaHeader) || size > partition->size ||
        requestSize < size || uint64_t(requestSize) > uint64_t(size) + 4096) {
      failOta(400); return;
    }
    if (!reserveOta()) { failOta(409); return; }
    if (!otaTransfer.begin(millis(), true, true, size, partition->size)) { failOta(400); return; }
    logLive("ota", "detection paused");
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (otaTransfer.failed) return;
    if (!otaTransfer.add(millis(), u.currentSize, Config::OTA_DEADLINE_MS)) { failOta(400); return; }
    size_t offset = 0;
    if (!otaBegun) {
      while (otaHeaderSize < sizeof(otaHeader) && offset < u.currentSize) otaHeader[otaHeaderSize++] = u.buf[offset++];
      if (otaHeaderSize < sizeof(otaHeader)) return;
      esp_image_header_t header; memcpy(&header, otaHeader, sizeof(header));
      esp_app_desc_t app;
      memcpy(&app, otaHeader + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(app));
      if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.chip_id != ESP_CHIP_ID_ESP32S3 ||
          app.magic_word != ESP_APP_DESC_MAGIC_WORD) { failOta(400); return; }
      if (!Update.begin(otaTransfer.expected, U_FLASH)) { failOta(500); return; }
      otaBegun = true;
      if (Update.write(otaHeader, sizeof(otaHeader)) != sizeof(otaHeader)) { failOta(500); return; }
    }
    if (offset < u.currentSize && Update.write(u.buf + offset, u.currentSize - offset) != u.currentSize - offset) failOta(500);
  } else if (u.status == UPLOAD_FILE_END) {
    if (!otaTransfer.end(millis(), Config::OTA_DEADLINE_MS)) failOta(400);
    // Do not select a boot partition here: another multipart file may still follow.
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    failOta(400);
  }
}
void finishFirmwareRequest() {
  if (otaResponseSent) { cleanupOta(); return; }
  if (!requireMutation()) { cleanupOta(); return; }
  const bool ready = otaRequestOpen && otaBegun && otaTransfer.ready(millis(), Config::OTA_DEADLINE_MS);
  const bool ok = ready && Update.end();
  if (ready && !ok) otaHttpError = 500;
  if (ok) otaBegun = false;
  privateHeaders(); web.sendHeader("Connection", "close");
  web.send(ok ? 200 : otaHttpError, "text/plain; charset=utf-8", ok ? "更新成功，重新啟動" : "更新拒絕或失敗；原韌體保留，請檢查入口、檔案與授權");
  if (ok) { logLive("ota", "completed"); delay(400); ESP.restart(); }
  cleanupOta();
}
void registerWebRoutes() {
  if (webRoutesReady) return;
  webRoutesReady = true;
  const char *headers[] = {"Authorization", "Host", "Origin", "X-CSRF-Token", "X-Firmware-Size", "Content-Length"};
  web.collectHeaders(headers, sizeof(headers) / sizeof(headers[0]));
  web.on("/", HTTP_GET, [](){ if (requireAdmin()) web.send(200, "text/html; charset=utf-8", mainHtml()); });
  web.on("/api/status", HTTP_GET, [](){ if (requireAdmin()) web.send(200, "application/json", statusJson()); });
  web.on("/diagnostics", HTTP_GET, [](){ if (requireAdmin()) web.send(200, "text/html; charset=utf-8", diagIndexHtml()); });
  web.on("/live", HTTP_GET, [](){ if (requireAdmin()) web.send(200, "text/plain; charset=utf-8", liveLogText()); });
  web.on("/diag/current", HTTP_GET, [](){ if (requireAdmin()) sendFilePath("/diag.jsonl", "application/x-ndjson; charset=utf-8", "diagnostics-current.jsonl"); });
  web.on("/diag/previous", HTTP_GET, [](){ if (requireAdmin()) sendFilePath("/diag.prev.jsonl", "application/x-ndjson; charset=utf-8", "diagnostics-previous.jsonl"); });
  web.on("/trace", HTTP_GET, [](){
    if (!requireAdmin()) return;
    uint32_t slot = 0;
    if (!Runtime::decimal(web.arg("slot").c_str(), slot) || slot >= Config::TRACE_SLOTS) { web.send(400, "text/plain", "bad slot"); return; }
    const String sid = web.arg("sid");
    if (web.hasArg("sid") && !Runtime::safeSessionId(sid.c_str())) { web.send(400, "text/plain", "bad session id"); return; }
    sendFilePath("/trace" + String(slot) + ".csv", "text/csv; charset=utf-8", "", sid);
  });
  web.on("/normal", HTTP_POST, [](){
    if (!requireMutation()) return;
    if (otaPaused.load()) { web.send(409, "text/plain", "update in progress"); return; }
    maintenanceSticky = false; maintenanceWindow.close(); csrfToken = randomToken(); nonceIssued = false;
    web.send(200, "text/plain; charset=utf-8", connectedStandby.load() ? "已回到連線省電待機" : "即將關閉網路，回到離線低功耗");
  });
  web.on("/update", HTTP_POST, finishFirmwareRequest, uploadFirmwareChunk);
  web.onNotFound([](){ if (requireAdmin()) web.send(404, "text/plain", "not found"); });
}

bool writeTrace(const DiagnosticSnapshot &d) {
  if (!spiffsReady) return false;
  const uint8_t slot = diagPrefs.getUChar("slot", 0) % Config::TRACE_SLOTS;
  const String target = "/trace" + String(slot) + ".csv", temp = "/trace.tmp";
  File f = SPIFFS.open(temp, FILE_WRITE);
  if (!f) return false;
  String block; block.reserve(8192);
  block = "session_id," + d.sid + "\nM,total_samples," + String(d.trace.total) + "\nM,retained_samples," + String(d.trace.count) +
    "\nM,overwritten_samples," + String(d.trace.overwritten()) + "\nM,dropped_events," + String(d.trace.eventsDropped) + "\nS,uptime_ms,distance_mm,flags(valid=1 blocked=2 rfid=4)\n";
  bool ok = true;
  auto flush = [&](){ if (block.length()) { ok = ok && f.print(block) == block.length(); block = ""; } };
  for (uint16_t i = 0; i < d.trace.count && ok; ++i) {
    const Trace::Sample &s = d.trace.at(i);
    block += "S," + String(s.ms) + "," + String(s.mm) + "," + String(s.flags) + "\n";
    if (block.length() >= 6000) flush();
  }
  block += "E,uptime_ms,code,value,aux\n";
  for (uint16_t i = 0; i < d.trace.eventCount && ok; ++i) {
    const Trace::Event &e = d.trace.events[i];
    block += "E," + String(e.ms) + "," + String(e.code) + "," + String(e.value) + "," + String(e.aux) + "\n";
    if (block.length() >= 6000) flush();
  }
  flush(); f.close();
  if (!ok) { SPIFFS.remove(temp); return false; }
  SPIFFS.remove(target);
  if (!SPIFFS.rename(temp, target)) return false;
  return diagPrefs.putUChar("slot", (slot + 1) % Config::TRACE_SLOTS) == 1;
}
const char *scanResultName(Visit::ScanResult r) {
  switch (r) {
    case Visit::ScanResult::None: return "none";
    case Visit::ScanResult::Scanning: return "scanning";
    case Visit::ScanResult::Valid: return "valid";
    case Visit::ScanResult::Rejected: return "rejected";
    case Visit::ScanResult::Timeout: return "timeout";
    case Visit::ScanResult::Cancelled: return "cancelled";
  }
  return "unknown";
}
String scanJson(const ScanMetrics &m) {
  return "{\"scans\":" + String(m.scans) + ",\"bytes\":" + String(m.bytes) + ",\"recognized\":" + String(m.recognized) +
    ",\"on_ms\":" + String(m.onMs) + ",\"first_byte_ms\":" + String(m.firstByteMs == UINT32_MAX ? 0 : m.firstByteMs) +
    ",\"valid_ms\":" + String(m.validMs == UINT32_MAX ? 0 : m.validMs) + ",\"last_result\":\"" + scanResultName(m.result) + "\"}";
}
void saveDiagnostics(const DiagnosticSnapshot &d) {
  ScopedLock guard(diagMutex);
  const bool traceSaved = writeTrace(d);
  const SessionMetrics &m = d.metrics;
  String j; j.reserve(1800);
  j = "{\"schema\":2,\"fw\":\"" + String(FW_VERSION) + "\",\"session_id\":\"" + jsonEscape(d.sid) + "\",\"reason\":\"" + Visit::reasonName(d.reason) + "\",";
  j += "\"cat\":\"" + jsonEscape(d.cat) + "\",\"duration_ms\":" + String(d.durationMs) + ",\"elapsed_ms\":" + String(d.elapsedMs);
  j += ",\"duration_valid\":" + String(d.reason == Visit::Reason::Normal && d.durationMs > 0 ? "true" : "false");
  j += ",\"record_queued\":" + String(d.recordQueued ? "true" : "false");
  j += ",\"samples\":" + String(m.samples) + ",\"invalid\":" + String(m.invalid) + ",\"max_gap_ms\":" + String(m.maxGap);
  j += ",\"global_max_gap_ms\":" + String(d.maxGlobalGap) + ",\"min_mm\":" + String(m.minMm == UINT16_MAX ? 0 : m.minMm);
  j += ",\"avg_mm\":" + String(m.samples ? uint32_t(m.sumMm / m.samples) : 0);
  j += ",\"scans\":" + String(m.scanCount) + ",\"recognized\":" + String(m.recognized) + ",\"rfid_on_ms\":" + String(m.rfidOnMs);
  j += ",\"first_byte_ms\":" + String(m.firstByteMinMs == UINT32_MAX ? 0 : m.firstByteMinMs) + ",\"valid_chip_ms\":" + String(m.validMinMs == UINT32_MAX ? 0 : m.validMinMs);
  j += ",\"bytes\":" + String(m.bytes) + ",\"bad_frames\":" + String(m.badFrames);
  j += ",\"sleep_ms\":" + String(m.sleepMs) + ",\"sleep_calls\":" + String(m.sleepCalls) + ",\"sleep_errors\":" + String(m.sleepErrors) + ",\"sleep_accounting\":\"manual_only\"";
  j += ",\"debug_web_server\":" + String(DEBUG_WEB_SERVER ? "true" : "false") + ",\"automatic_pm_configured\":" + String(automaticPm.load() ? "true" : "false");
  j += ",\"trace_saved\":" + String(traceSaved ? "true" : "false") + ",\"trace_retained\":" + String(d.trace.count) + ",\"trace_overwritten\":" + String(d.trace.overwritten()) + ",\"trace_events_dropped\":" + String(d.trace.eventsDropped);
  j += ",\"entry_scan\":" + scanJson(m.entryScan) + ",\"exit_scan\":" + scanJson(m.exitScan) + "}";
  appendDiag(j);
}
void stopNetwork() {
  if (webRunning) { web.stop(); webRunning = false; }
  cleanupOta(); csrfToken = ""; nonceIssued = false;
  WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF); netMode.store(NetMode::Off);
}
void networkTask(void *) {
  uint32_t lastUpload = 0;
  bool uploadRequested = true;
  for (;;) {
    {
      // Serializes transitions against OFF-mode manual sleep, not against sampling.
      ScopedLock power(powerGate);
      const uint32_t request = maintenanceRequest.exchange(0);
      if (request) { maintenanceWindow.open(millis(), request); uploadRequested = true; }
      uint8_t diagnostic = 0;
      if (xQueueReceive(readyDiagnostics, &diagnostic, 0) == pdTRUE) {
        saveDiagnostics(diagnosticSnapshots[diagnostic]);
        xQueueSend(freeDiagnostics, &diagnostic, 0);
      }
      const bool wanted = connectedStandby.load() || (maintenanceSticky && adminConfigured()) || maintenanceWindow.active(millis());
      if (netMode.load() == NetMode::Sta && WiFi.status() != WL_CONNECTED) {
        stopNetwork(); reconnectBackoff.fail(millis(), Config::NETWORK_RETRY_INITIAL_MS, Config::NETWORK_RETRY_MAX_MS);
      }
      if (wanted && netMode.load() == NetMode::Off && reconnectBackoff.ready(millis())) {
        if (connectSta(Config::WIFI_TIMEOUT_MS)) {
          reconnectBackoff.reset();
          if (adminConfigured()) {
            csrfToken = randomToken(); nonceIssued = false;
            registerWebRoutes(); web.begin(); webRunning = true;
            logLive("web", WiFi.localIP().toString());
            Serial.print("Management URL: http://"); Serial.println(WiFi.localIP());
          } else logLive("web_disabled", "set independent admin credentials");
          uploadRequested = true;
        } else reconnectBackoff.fail(millis(), Config::NETWORK_RETRY_INITIAL_MS, Config::NETWORK_RETRY_MAX_MS);
      }
      if (webRunning) web.handleClient();
      if (otaRequestOpen && (otaTransfer.expired(millis(), Config::OTA_DEADLINE_MS) || !web.client().connected())) cleanupOta();
      const bool keepNetwork = connectedStandby.load() || (maintenanceSticky && adminConfigured()) || maintenanceWindow.active(millis());
      if (!keepNetwork && !otaRequestOpen && netMode.load() != NetMode::Off) stopNetwork();
      if (!otaRequestOpen && netMode.load() == NetMode::Sta &&
          (uploadRequested || uint32_t(millis() - lastUpload) >= Config::UPLOAD_RETRY_MS)) {
        uploadRequested = false; lastUpload = millis();
        uploadPending(); // One record; service the management listener between uploads.
      }
    }
    delay(Config::NETWORK_POLL_MS);
  }
}
