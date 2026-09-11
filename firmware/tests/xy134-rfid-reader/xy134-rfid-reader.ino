#include <WiFi.h>
#include <WebServer.h>
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif


// ============================================================
// XIAO ESP32-S3
// ============================================================

constexpr int RFID_ENABLE_PIN = D0;
constexpr int RFID_RX_PIN = D7;  // XY TXD -> XIAO D7/RX

HardwareSerial RFID(1);
WebServer server(80);


// ============================================================
// 已知貓咪晶片
// ============================================================

struct PetTag {
  const char* id;
  const char* name;
};

#ifndef CAT_1_CHIP_RAW
#define CAT_1_CHIP_RAW ""
#define CAT_1_NAME "unknown"
#define CAT_2_CHIP_RAW ""
#define CAT_2_NAME "unknown"
#endif

const PetTag PETS[] = {
  { CAT_1_CHIP_RAW, CAT_1_NAME },
  { CAT_2_CHIP_RAW, CAT_2_NAME },
};

constexpr size_t PET_COUNT = sizeof(PETS) / sizeof(PETS[0]);


// ============================================================
// RFID 狀態
// ============================================================

String rxBuffer;

String lastRaw = "";
String lastType = "";
String lastId = "";
String lastPetName = "";
String lastChecksum = "";

bool lastChecksumValid = false;
bool hasTag = false;

unsigned long lastReadMillis = 0;


// ============================================================
// Helper
// ============================================================

String getPetName(const String& id) {
  for (size_t i = 0; i < PET_COUNT; i++) {
    if (PETS[i].id[0] != '\0' && id == PETS[i].id) {
      return PETS[i].name;
    }
  }

  return "未知晶片";
}

String formatTagId(const String& id) {
  if (id.length() != 15) {
    return id;
  }

  return id.substring(0, 5)
       + "-"
       + id.substring(5, 10)
       + "-"
       + id.substring(10, 15);
}

String jsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\n", "\\n");
  value.replace("\r", "\\r");
  return value;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;

  return -1;
}


// ============================================================
// Parse RFID frame
//
// 例如：
// $F123456789012345XX#
// ============================================================

void parseFrame(const String& frame) {
  lastRaw = frame;
  lastReadMillis = millis();
  lastType = "";
  lastId = "";
  lastPetName = "";
  lastChecksum = "";
  lastChecksumValid = false;
  hasTag = false;

  if (frame.length() != 20 || !frame.startsWith("$F") || !frame.endsWith("#")) {
    return;
  }

  String body = frame.substring(1, frame.length() - 1);

  if (body.length() != 18 || body[0] != 'F') {
    return;
  }

  for (size_t i = 1; i <= 15; i++) {
    if (body[i] < '0' || body[i] > '9') {
      return;
    }
  }

  lastType = body.substring(0, 1);

  lastChecksum = body.substring(
    body.length() - 2
  );

  lastId = body.substring(
    1,
    body.length() - 2
  );

  // Checksum
  uint8_t calculatedChecksum = 0;

  for (size_t i = 0; i < body.length() - 2; i++) {
    calculatedChecksum ^= (uint8_t)body[i];
  }

  int high = hexValue(lastChecksum[0]);
  int low = hexValue(lastChecksum[1]);

  if (high < 0 || low < 0) {
    return;
  }

  uint8_t receivedChecksum = (high << 4) | low;
  lastChecksumValid = calculatedChecksum == receivedChecksum;
  if (!lastChecksumValid) {
    return;
  }

  lastPetName = getPetName(lastId);
  hasTag = true;

  Serial.println();
  Serial.println("================================");
  Serial.print("貓咪: ");
  Serial.println(lastPetName);

  Serial.print("晶片: ");
  Serial.println(formatTagId(lastId));

  Serial.print("RAW: ");
  Serial.println(lastRaw);

  Serial.print("Checksum: ");
  Serial.println(lastChecksumValid ? "OK" : "ERROR");

  Serial.println("================================");
}


// ============================================================
// UART reader
// ============================================================

void readRFID() {
  while (RFID.available()) {
    char c = (char)RFID.read();

    Serial.write(c);

    if (c == '$') {
      rxBuffer = "$";
      continue;
    }

    if (rxBuffer.length() == 0) {
      continue;
    }

    rxBuffer += c;

    if (c == '#') {
      parseFrame(rxBuffer);
      rxBuffer = "";
      continue;
    }

    if (rxBuffer.length() > 100) {
      rxBuffer = "";
    }
  }
}


// ============================================================
// API
// ============================================================

void handleStatus() {
  String json = "{";

  json += "\"hasTag\":";
  json += hasTag ? "true" : "false";

  json += ",\"name\":\"";
  json += jsonEscape(lastPetName);
  json += "\"";

  json += ",\"id\":\"";
  json += jsonEscape(formatTagId(lastId));
  json += "\"";

  json += ",\"raw\":\"";
  json += jsonEscape(lastRaw);
  json += "\"";

  json += ",\"type\":\"";
  json += jsonEscape(lastType);
  json += "\"";

  json += ",\"checksum\":\"";
  json += jsonEscape(lastChecksum);
  json += "\"";

  json += ",\"checksumValid\":";
  json += lastChecksumValid ? "true" : "false";

  json += "}";

  server.send(
    200,
    "application/json; charset=utf-8",
    json
  );
}


// ============================================================
// Web
// ============================================================

void handleRoot() {
  const char page[] PROGMEM = R"HTML(
<!DOCTYPE html>

<html lang="zh-Hant">

<head>

<meta charset="UTF-8">

<meta
  name="viewport"
  content="width=device-width, initial-scale=1"
>

<title>貓咪晶片掃描器</title>

<style>

body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  max-width: 700px;
  margin: 40px auto;
  padding: 0 20px;
  background: #f5f5f5;
}

h1 {
  margin-bottom: 25px;
}

.card {
  background: white;
  border-radius: 12px;
  padding: 20px;
  margin-bottom: 16px;
  box-shadow: 0 2px 8px rgba(0,0,0,.08);
}

.label {
  color: #777;
  font-size: 14px;
}

.name {
  font-size: 42px;
  font-weight: 700;
}

.id {
  font-size: 28px;
  font-weight: 600;
}

.good {
  color: green;
}

.bad {
  color: red;
}

.waiting {
  color: #888;
}

pre {
  white-space: pre-wrap;
  word-break: break-all;
}

</style>

</head>

<body>

<h1>貓咪晶片掃描器</h1>


<div class="card">

  <div class="label">
    貓咪
  </div>

  <div
    id="petName"
    class="name waiting"
  >
    等待掃描...
  </div>

</div>


<div class="card">

  <div class="label">
    晶片編號
  </div>

  <div
    id="tagId"
    class="id"
  >
    -
  </div>

</div>


<div class="card">

  <div class="label">
    資料驗證
  </div>

  <div id="checksumStatus">
    -
  </div>

</div>


<div class="card">

  <div class="label">
    原始讀取資料
  </div>

  <pre id="raw">尚未收到資料</pre>

</div>


<script>

async function update() {

  try {

    const response =
      await fetch("/api/status");

    const data =
      await response.json();


    if (!data.hasTag) {
      return;
    }


    const name =
      document.getElementById("petName");

    name.textContent =
      data.name;

    name.classList.remove("waiting");


    document.getElementById(
      "tagId"
    ).textContent =
      data.id;


    document.getElementById(
      "raw"
    ).textContent =
      data.raw;


    const checksum =
      document.getElementById(
        "checksumStatus"
      );


    if (data.checksumValid) {

      checksum.textContent =
        "校驗碼正確";

      checksum.className =
        "good";

    } else {

      checksum.textContent =
        "校驗碼錯誤";

      checksum.className =
        "bad";

    }

  } catch (error) {

    console.log(error);

  }

}

setInterval(update, 500);

update();

</script>

</body>

</html>
)HTML";

  server.send(
    200,
    "text/html; charset=utf-8",
    page
  );
}


// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println("Cat RFID Scanner");


  // ON/OFF -> D0
  pinMode(
    RFID_ENABLE_PIN,
    OUTPUT
  );

  digitalWrite(
    RFID_ENABLE_PIN,
    HIGH
  );

  Serial.println(
    "XY-134.2K enabled"
  );

  delay(200);


  // ----------------------------------------------------------
  // 唯讀 UART
  //
  // XY TXD -> D7
  //
  // TX 設 -1，因此 ESP32 不會向 RFID 模組發指令
  // ----------------------------------------------------------

  RFID.begin(
    9600,
    SERIAL_8N1,
    RFID_RX_PIN,
    -1
  );

  Serial.println(
    "RFID UART ready"
  );


  // Wi-Fi
  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  Serial.print(
    "Connecting WiFi"
  );

  while (
    WiFi.status() != WL_CONNECTED
  ) {

    delay(500);

    Serial.print(".");
  }


  Serial.println();

  Serial.print(
    "Web: http://"
  );

  Serial.println(
    WiFi.localIP()
  );


  server.on(
    "/",
    handleRoot
  );

  server.on(
    "/api/status",
    handleStatus
  );

  server.begin();


  Serial.println(
    "Ready - scan cat..."
  );
}


// ============================================================
// LOOP
// ============================================================

void loop() {
  server.handleClient();

  readRFID();
}
