#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <VL53L0X.h>
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

VL53L0X sensor;
WebServer server(80);

uint16_t currentDistance = 0;
bool sensorTimeout = false;

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-TW">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>貓砂櫃距離測試</title>

  <style>
    body {
      font-family: system-ui, sans-serif;
      background: #f5f5f5;
      margin: 0;
      padding: 24px;
      text-align: center;
    }

    .card {
      max-width: 420px;
      margin: 40px auto;
      background: white;
      border-radius: 20px;
      padding: 32px;
      box-shadow: 0 8px 30px rgba(0,0,0,0.08);
    }

    h1 {
      font-size: 24px;
      margin-bottom: 28px;
    }

    .distance {
      font-size: 64px;
      font-weight: 700;
    }

    .unit {
      font-size: 24px;
      color: #666;
    }

    .status {
      margin-top: 20px;
      font-size: 18px;
      color: #555;
    }

    .small {
      margin-top: 28px;
      font-size: 14px;
      color: #999;
    }
  </style>
</head>

<body>

<div class="card">
  <h1>貓砂櫃距離測試</h1>

  <div>
    <span id="distance" class="distance">--</span>
    <span class="unit">mm</span>
  </div>

  <div id="status" class="status">
    讀取中...
  </div>

  <div class="small">
    每 500ms 自動更新
  </div>
</div>

<script>
async function updateDistance() {
  try {
    const response = await fetch('/distance');
    const data = await response.json();

    const distanceEl = document.getElementById('distance');
    const statusEl = document.getElementById('status');

    if (data.timeout) {
      distanceEl.textContent = '--';
      statusEl.textContent = '感測器 Timeout';
      return;
    }

    distanceEl.textContent = data.distance;

    if (data.distance < 300) {
      statusEl.textContent = '非常靠近';
    } else if (data.distance < 500) {
      statusEl.textContent = '靠近';
    } else if (data.distance < 800) {
      statusEl.textContent = '中距離';
    } else {
      statusEl.textContent = '遠 / 無物體';
    }

  } catch (error) {
    document.getElementById('status').textContent = 'ESP32 連線中斷';
  }
}

setInterval(updateDistance, 500);
updateDistance();
</script>

</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleDistance() {
  String json = "{";
  json += "\"distance\":" + String(currentDistance) + ",";
  json += "\"timeout\":";
  json += sensorTimeout ? "true" : "false";
  json += "}";

  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // VL53L0X
  // SDA = D4 = GPIO5
  // SCL = D5 = GPIO6
  Wire.begin(5, 6);

  sensor.setTimeout(500);

  if (!sensor.init()) {
    Serial.println("VL53L0X 初始化失敗");
    while (1) {
      delay(1000);
    }
  }

  sensor.startContinuous(100);

  Serial.println("VL53L0X 初始化成功");

  // Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("連接 WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi 已連線");
  Serial.print("開啟瀏覽器： http://");
  Serial.println(WiFi.localIP());

  // Web Server
  server.on("/", handleRoot);
  server.on("/distance", handleDistance);

  server.begin();

  Serial.println("Web Server 已啟動");
}

void loop() {
  server.handleClient();

  currentDistance = sensor.readRangeContinuousMillimeters();
  sensorTimeout = sensor.timeoutOccurred();

  delay(20);
}
