# 貓砂櫃主韌體

功能：偵測進出，以 ISO11784/85 晶片辨識並上傳 Google Sheets。
檔案：main.ino 主程式；app_config.h 設定；certificates.h 公開憑證；secrets.example.h 空白範本；secrets.h 僅限本機。採購、供電、接線與讀距資料以 `../../hardware/README.md` 為準。
接線：依 Cirkit Designer 電路圖，XY ON/OFF=GPIO1（D0）；ToF INT/SDA/SCL=GPIO2/5/6（D1/D4/D5）；ESP32 TX→XY RXD=GPIO43（D6）；ESP32 RX←XY TXD=GPIO44（D7）；所有模組共地。RFID 的 VCC 不由 GPIO 供電，A1/A2 只接指定天線，模組電源附近保留電解與陶瓷去耦電容。
流程：距離首次小於 200mm 立即啟動進入 RFID 掃描，同時以 300ms 防抖確認事件；首次大於等於 200mm 立即再次掃描，同時以 500ms 防抖確認離開。每次最多 3 秒。第二次漏讀保留第一次身分，第一次漏讀可由第二次補上，兩次漏讀才是 `unknown`。距離在離開防抖期間回到小於 200mm 就繼續同一事件；未確認進入的短暫觸發不寫入紀錄。待機1000ms、活動100ms。
XY：9600 8N1，封包 $F＋15位數字＋XOR＋#。上傳失敗存 NVS（8筆），活動中的事件每30秒備份。
IDE：安裝 ESP32、Pololu VL53L0X，選 XIAO ESP32-S3；只在本機 secrets.h 填秘密，再 Verify、Upload，序列埠115200。

除錯：在本機 `secrets.h` 設定 `DEBUG_WEB_SERVER true` 後重新編譯。裝置會在序列埠印出區域網路網址；頁面即時顯示距離、進入掃描／離開掃描、RFID 原始資料與校驗、狀態機、目前紀錄、Wi-Fi 與上傳佇列。公開範本預設為 `false`；除錯模式會讓 Wi-Fi 常駐，不適合電池正式運作，也不應將頁面開放至網際網路。

Google Apps Script 的 POST 與 ContentService 重新導向分別建立 HTTPS 連線，並信任 Google R1/R4 憑證鏈。成功後會依序補送 NVS 中的紀錄；`session_id` 可避免重試產生重複列。

---

# Main litter-cabinet firmware

Purpose: detect visits, identify ISO11784/85 chips, and upload one completed visit to Google Sheets. `main.ino` contains the application; `app_config.h` owns pins and timing; `certificates.h` contains public CA certificates; `secrets.example.h` is the public interface; and ignored `secrets.h` contains local deployment values. See `../../hardware/README.md` for sourcing, power, wiring, and measured RFID range.

Wiring: XY ON/OFF is GPIO1 (D0); ToF INT/SDA/SCL are GPIO2/5/6 (D1/D4/D5); ESP32 TX to XY RXD is GPIO43 (D6); ESP32 RX from XY TXD is GPIO44 (D7). All modules share ground. Do not power RFID from a GPIO, and connect A1/A2 only to the specified antenna.

Runtime: the first reading below 200mm starts RFID immediately alongside the 300ms entry debounce. The first reading at or above 200mm starts another scan alongside the 500ms exit debounce. Each scan lasts at most three seconds. An exit miss preserves the entry identity; an exit success can identify an entry miss; two misses remain `unknown`. A return below 200mm during exit debounce continues the same visit. Unconfirmed entry triggers produce no record. Idle sampling is 1000ms and active sampling is 100ms.

Reliability: an active visit is checkpointed every 30 seconds and up to eight failed deliveries are queued in NVS. The Apps Script POST and ContentService redirect use separate HTTPS connections with the Google R1/R4 CA bundle. Successful delivery drains the queue in order, while `session_id` makes retries idempotent.

Arduino IDE: install the ESP32 platform and Pololu VL53L0X library, select XIAO ESP32-S3, place values only in local `secrets.h`, then Verify and Upload. Serial output uses 115200 baud.

Debugging: set `DEBUG_WEB_SERVER true` in local `secrets.h` and rebuild. The printed LAN page shows distance, entry/exit RFID phase, raw UART and validation, state, current visit, Wi-Fi, latest upload result, and pending queue. The public example defaults to `false`; debug mode keeps Wi-Fi active and must remain on a trusted LAN.
