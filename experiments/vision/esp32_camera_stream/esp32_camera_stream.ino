#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// Copy secrets.example.h to secrets.h and fill it only on the flashing computer.

// ===== XIAO ESP32-S3 Sense 相機接腳定義 (官方固定值，不要更動) =====
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

#include "esp_http_server.h"

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

// ===== 首頁 HTML：即時串流 + Label 選擇 + 拍照 =====
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>貓咪資料集拍攝</title>
  <style>
    body { background:#111; color:#eee; font-family:sans-serif; text-align:center; margin:0; padding:16px; }
    img { max-width:100%; border:2px solid #444; margin-top:12px; border-radius:8px; }
    .labels { margin-top:20px; display:flex; flex-wrap:wrap; gap:10px; justify-content:center; }
    button { font-size:18px; padding:14px 22px; border-radius:10px; border:none; cursor:pointer; }
    .label-btn { background:#2d6cdf; color:#fff; }
    .label-btn:active { background:#1c4faa; }
    input[type=text] { font-size:16px; padding:10px; border-radius:8px; border:1px solid #555; width:60%; }
    .custom-btn { background:#555; color:#fff; margin-left:8px; }
    #status { margin-top:16px; font-size:18px; min-height:24px; }
    .ok { color:#4caf50; }
    .err { color:#f44336; }
  </style>
</head>
<body>
  <h2>貓咪資料集拍攝</h2>
  <img src="/stream">

  <div class="labels">
    <button class="label-btn" onclick="capture('翎角')">翎角</button>
    <button class="label-btn" onclick="capture('麻嚕')">麻嚕</button>
    <button class="label-btn" onclick="capture('unknown')">未知</button>
  </div>

  <div id="status"></div>
  <div id="count"></div>

<script>
function setStatus(msg, cls) {
  const el = document.getElementById('status');
  el.innerText = msg;
  el.className = cls || '';
}

function setCount(label, count) {
  document.getElementById('count').innerText = label + ' 目前累積: ' + count + ' 張';
}

function capture(label) {
  setStatus('拍照上傳中... (' + label + ')');
  fetch('/capture?label=' + encodeURIComponent(label))
    .then(r => r.json())
    .then(d => {
      if (d.status === 'ok') {
        setStatus('✅ 已上傳: ' + label, 'ok');
        setCount(d.label || label, d.count !== undefined ? d.count : '?');
      } else {
        setStatus('❌ 上傳失敗: ' + (d.message || ''), 'err');
      }
    })
    .catch(e => setStatus('❌ 連線錯誤: ' + e, 'err'));
}
</script>
</body>
</html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char*)INDEX_HTML, strlen(INDEX_HTML));
}

// ===== MJPEG Stream Handler =====
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t hlen = 0;
  char part_buf[64];

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("相機擷取失敗");
      res = ESP_FAIL;
    } else {
      if (fb->format != PIXFORMAT_JPEG) {
        Serial.println("非 JPEG 格式，略過");
        esp_camera_fb_return(fb);
        continue;
      }
    }

    if (res == ESP_OK) {
      hlen = snprintf(part_buf, 64, STREAM_PART, fb->len);
      res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    }

    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
    }
    if (res != ESP_OK) break;
    delay(30); // 降低擷取頻率，避免 QSPI PSRAM 頻寬跟不上造成溢出
  }
  return res;
}

// ===== 拍照並上傳到 PC Dataset Server =====
static esp_err_t capture_handler(httpd_req_t *req) {
  // 從網址參數解析 label，例如 /capture?label=翎角
  char label[64] = "unknown";
  char query[128];
  if (httpd_req_get_url_query_len(req) > 0 &&
      httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char param[64];
    if (httpd_query_key_value(query, "label", param, sizeof(param)) == ESP_OK) {
      // URL decode（處理中文與特殊字元）
      char decoded[64];
      int j = 0;
      for (int i = 0; param[i] != '\0' && j < 63; i++) {
        if (param[i] == '%') {
          if (!param[i+1] || !param[i+2]) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"invalid label encoding\"}");
            return ESP_ERR_INVALID_ARG;
          }
          const char high = param[i + 1];
          const char low = param[i + 2];
          const bool highIsHex = isxdigit(static_cast<unsigned char>(high));
          const bool lowIsHex = isxdigit(static_cast<unsigned char>(low));
          if (!highIsHex || !lowIsHex) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"invalid label encoding\"}");
            return ESP_ERR_INVALID_ARG;
          }
          int hex = 0;
          sscanf(&param[i + 1], "%2x", &hex);
          decoded[j++] = (char)hex;
          i += 2;
        } else if (param[i] == '+') {
          decoded[j++] = ' ';
        } else {
          decoded[j++] = param[i];
        }
      }
      decoded[j] = '\0';
      strncpy(label, decoded, sizeof(label) - 1);
    }
  }

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("拍照失敗：無法取得畫面");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"camera capture failed\"}");
    return ESP_FAIL;
  }

  // 組 multipart/form-data 內容
  String boundary = "----ESP32CatBoundary";
  String head = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"label\"\r\n\r\n";
  head += String(label) + "\r\n";
  head += "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"file\"; filename=\"capture.jpg\"\r\n";
  head += "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  size_t totalLen = head.length() + fb->len + tail.length();
  uint8_t *body = (uint8_t*)malloc(totalLen);

  bool ok = false;
  int httpCode = -1;
  String responseBody = "";

  if (body) {
    memcpy(body, head.c_str(), head.length());
    memcpy(body + head.length(), fb->buf, fb->len);
    memcpy(body + head.length() + fb->len, tail.c_str(), tail.length());

    HTTPClient http;
    String url = "http://" + String(PC_SERVER_IP) + ":" + String(PC_SERVER_PORT) + "/upload";
    http.setConnectTimeout(5000);
    http.setTimeout(10000);
    if (http.begin(url)) {
      http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
      httpCode = http.POST(body, totalLen);
      if (httpCode == 200) {
        responseBody = http.getString(); // PC 回傳的 JSON（含 status/filename/label/count）
      }
      http.end();
    } else {
      Serial.println("HTTP 初始化失敗");
      httpCode = -2;
    }
    free(body);
    ok = (httpCode == 200);
  } else {
    Serial.println("記憶體配置失敗，無法組上傳封包");
  }

  esp_camera_fb_return(fb);

  httpd_resp_set_type(req, "application/json");
  if (ok && responseBody.length() > 0) {
    Serial.printf("上傳成功: label=%s, PC回應=%s\n", label, responseBody.c_str());
    httpd_resp_sendstr(req, responseBody.c_str());
    return ESP_OK;
  } else {
    Serial.printf("上傳失敗: label=%s, http code=%d\n", label, httpCode);
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"status\":\"error\",\"message\":\"http code %d\"}", httpCode);
    httpd_resp_sendstr(req, resp);
    return ESP_FAIL;
  }
}

// ===== 啟動 Web Server =====
void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t capture_uri = {
    .uri       = "/capture",
    .method    = HTTP_GET,
    .handler   = capture_handler,
    .user_ctx  = NULL
  };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &stream_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // ===== 相機設定 =====
  camera_config_t config{};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // 除錯用：確認 PSRAM 是否真的被偵測到
  Serial.printf("PSRAM 是否偵測到: %s\n", psramFound() ? "是" : "否");
  Serial.printf("PSRAM 可用大小: %lu bytes\n",
                static_cast<unsigned long>(ESP.getPsramSize()));

  // 如果有 PSRAM，用中等畫質（QSPI PSRAM 頻寬有限，VGA+雙緩衝會導致 FB-OVF 溢出當機）
  if (psramFound() && ESP.getPsramSize() > 0) {
    config.frame_size = FRAMESIZE_QVGA;  // 320x240，QSPI PSRAM 下較穩定
    config.jpeg_quality = 12;
    config.fb_count = 1;                 // 單緩衝，避免溢出
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    // 沒有 PSRAM 時，用極保守設定，先求能跑
    config.frame_size = FRAMESIZE_QQVGA; // 160x120
    config.jpeg_quality = 20;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("相機初始化失敗，錯誤代碼 0x%x\n", err);
    return;
  }
  Serial.println("相機初始化成功");

  // ===== Wi-Fi 連線 =====
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("連線中");
  const unsigned long wifiDeadline = millis() + 15000;
  while (WiFi.status() != WL_CONNECTED && millis() < wifiDeadline) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWi-Fi 連線逾時");
    return;
  }
  Serial.println();
  Serial.print("已連線！IP位址: ");
  Serial.println(WiFi.localIP());

  startCameraServer();
  Serial.println("Web Server 已啟動，用瀏覽器打開上面的 IP 位址即可看到 Live View");
}

void loop() {
  delay(10000);
}
