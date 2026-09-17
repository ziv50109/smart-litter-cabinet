# Main firmware

`firmware/main/` 是 XIAO ESP32S3 的 RFID + VL53L0X 韌體，提供入口偵測、身分辨識、NVS 待傳紀錄、Google Sheets 上傳、診斷下載與受管理帳密保護的 Web OTA。

## 設定與編譯

複製 `secrets.example.h` 為本機 `secrets.h`，填入 Wi-Fi、Apps Script URL/token、貓咪晶片 ID／名稱，以及獨立的 `WEB_ADMIN_USER`、`WEB_ADMIN_PASSWORD`。管理密碼至少 8 字元；不要共用 Wi-Fi 密碼或裝置 token。舊的 `secrets.h` 也需要補上管理帳密，否則管理服務不會開放。

一般離線模式使用 Arduino-ESP32 3.3.11（或相容的較新版本）與 Pololu VL53L0X 1.3.1，Arduino IDE 選 `XIAO_ESP32S3` 後開啟 `main.ino`。保留含 `otadata`、兩個 OTA app 分區及 SPIFFS 的分割區配置。成功啟動管理服務時，序列埠會印出管理網址。

```sh
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 firmware/main
python firmware/tests/main_host/run.py
```

主機端測試不是 ESP32 編譯或實機驗收。工作流程分別檢查主機端邏輯、Arduino sketch 與自訂 SDK 建置；量距、UART 收訊、瀏覽器 Digest／OTA、斷線重連及平均電流仍需在安裝好的裝置驗證。

## 事件判定

所有可調門檻、容量與時間參數以 `app_config.h` 為唯一來源，文件不複製另一套數值。

首次有效遮擋建立暫定事件並開始 RFID 掃描。入口持續清空滿足條件後，下一次有效的清空轉遮擋建立離開候選；必要時進行離開側 RFID 掃描。入口再次完成清空確認且掃描結束，事件才以 `normal_exit` 結案。

停留時間是首次遮擋到離開候選的間隔，不包含離開側 RFID 或網路收尾。只有正常結案、具有已登錄身分且沒有身分衝突的事件才建立 Sheet 紀錄。入場與離開掃描的補身分／衝突檢查保留；無效量測或過長取樣間隔不算持續清空。入口清空不代表櫃內一定沒有貓。

## 執行與資料所有權

Arduino loop 單獨擁有感測器、RFID、`visit` 與進行中的統計。另一個 FreeRTOS task 負責 Wi-Fi、NTP、HTTP 上傳、診斷檔案及管理服務；同步網路等待不在量距執行路徑上。跨 task 的感測快照／OTA 預約、NVS 佇列、即時 log 與手動睡眠分別同步；不在 HTTP 傳輸期間持有 NVS 或感測鎖。

可信事件先寫入 NVS，再交由網路 task 傳送；上傳確認後更新環形佇列索引。未取得時鐘時，先保存帶本次 boot 識別的單調時間，於同一次開機取得 NTP 後轉成時間戳並保存，再上傳。沒有可信時間的舊紀錄會保留並回報 `timestamp unavailable`，不猜測日期；未同步時就斷電的時間無法事後從另一個 boot 還原。

完整診斷透過固定容量快照佇列交接。佇列滿時不阻塞偵測，`diagnostics_dropped` 與即時 `diagnostic_overflow` 顯示遺失；已寫入 NVS 的使用紀錄不受診斷快照遺失影響。Flash 寫入及系統排程仍可能造成延遲，因此狀態提供跨事件的 `global_max_gap_ms`，不以「使用另一個 task」取代量測驗證。

## 網路與省電

正式韌體只使用 STA，連線失敗、斷線及除錯模式都不建立 AP。斷線依 `app_config.h` 的退避與期限重試；離線模式的維護窗口結束後關閉 Wi-Fi。暫時的路由器斷線可以重連，Wi-Fi 設定遺失或管理密碼忘記則以 USB 為最後救援。

`LITTER_CONNECTED_STANDBY` 是獨立的編譯期模式選擇，不是除錯旗標：

| 模式 | 管理入口 | 省電方式 |
|---|---|---|
| `0`（預設） | 開機／事件後維護窗口 | 網路關閉時，利用取樣間隔手動 Light-sleep |
| `1` | STA 在線時保留監聽 | Wi-Fi Modem-sleep 與 SDK 自動 Light-sleep |

連線模式要求有效管理帳密、真正啟用的 `CONFIG_PM_ENABLE`／`CONFIG_FREERTOS_USE_TICKLESS_IDLE`，以及成功的 PM 初始化。缺少能力會回到離線模式，並顯示 `requested_mode`、`effective_mode`、`power_init`；不以 sketch 的同名巨集假裝啟用了預編譯 SDK 功能。

自動淺眠模式不呼叫手動睡眠／timer wakeup；RFID 收訊期間持有防淺眠及 APB 時鐘鎖，關閉 RFID 後釋放。網路 task 以有界輪詢間隔等待，感測 loop 按下一次取樣期限等待。`automatic_pm_configured` 只代表 API 設定成功，不代表已量到節電效果。

`DEBUG_WEB_SERVER=true` 只保留除錯維護窗口，不能繞過帳密驗證或重新允許 AP。`/normal` 在離線模式關閉網路，在連線模式則保留監聽並結束本次管理操作。網頁在背景時停止狀態輪詢；未授權請求不延長維護窗口。

### 自訂 SDK 建置

`firmware/idf/` 使用同一份 sketch，提供啟用 PM 的 ESP-IDF 5.5.5 建置入口；Arduino core 與 VL53L0X 來源版本／commit 由 `build.py` 核對，第三方原始碼只下載到被忽略的目錄，不覆蓋已有修改。ESP-IDF Component Manager 管理傳遞相依，實際解析結果記錄在其產生的 `dependencies.lock`。

在已啟用的 ESP-IDF 5.5.5 終端中執行：

```sh
python firmware/idf/build.py --connected
```

未加 `--connected` 仍建置離線預設。輸出為 `firmware/idf/build/smart_litter_cabinet.bin`。腳本只建置，不燒錄；第一次採用這個配置時，先核對裝置的 OTA/SPIFFS 分割區相容性，不把 merged、bootloader 或 partitions 映像當作 OTA app 上傳。

## 管理驗證與 Web OTA

瀏覽器直接開管理頁，使用 HTTP Digest 輸入帳密，不需安裝憑證。首頁、狀態 API、診斷、trace、即時 log、結束管理與 OTA 均經伺服器端驗證；不接受 Basic 降級。寫入操作另檢查 Origin 與自訂 header 中的 CSRF token。修改操作使用管理頁發送，沒有任意來源的 CORS。

OTA 在寫入前確認授權、請求大小、入口新鮮且持續清空，並取得互斥的更新權。進行中事件不允許 OTA。更新期間暫停偵測，請勿讓貓使用；失敗後重新等待入口清空再恢復判定。

只接受單一應用程式映像，檢查 ESP32-S3 image header、app descriptor、宣告大小、實際位元組數及交易期限。多檔、截斷、超量、中斷或驗證失敗不切換開機分區。整份 multipart 請求完成後才執行 `Update.end()`；成功才重新啟動。這不等於韌體簽章，也不宣稱具備新韌體開機後的自動 rollback。

HTTP Digest 不加密診斷或 `.bin` 傳輸，韌體也可能包含部署機密。本模式供可信任的居家區網使用，不直接對外開放或轉發管理埠；正式建置不要啟用會輸出認證細節的 core verbose log。

### HTTPS 安全性提升

需要傳輸保密與防竄改時，可改用支援 TLS 的 HTTPS server，配置裝置憑證並建立瀏覽器信任。HTTPS 能保護密碼驗證以外的診斷與韌體內容；本版沒有實作 HTTPS 開關，不要求安裝憑證，也不把忽略憑證警告當作正常流程。

## 診斷下載

`/diagnostics` 列出實際存在的追蹤檔，按紀錄編號命名，連結附上身分核對；槽位已被覆蓋會回報衝突，避免舊頁面下載到另一筆。

| 路徑 | 內容／檔名 |
|---|---|
| `/diag/current` | `diagnostics-current.jsonl`，每行一筆 JSON |
| `/diag/previous` | `diagnostics-previous.jsonl`，前一份大小輪替封存，不是上一個事件 |
| `/trace?slot=<n>&sid=<session_id>` | `<session_id>.csv` |
| `/api/status` | 狀態、模式、SDK 版本、跨事件取樣間隔與診斷遺失數 |
| `/live` | 有界 RAM 即時 log |

下載回應有明確的 Content-Disposition、副檔名與不快取標頭。先讀取有大小上限的不可變快照再傳送；記憶體不足會回報失敗，不邊下載邊混用新資料。

Trace 由 `M` 中繼資料、`S` 取樣與 `E` 事件區段組成。取樣容量按設定的事件最終期限與週期配置；總數、保留數、覆蓋數與事件溢出都有標示。事件碼如下：

| code | 意義 |
|---|---|
| 1／2 | RFID 開／關 |
| 3 | 掃描開始，value 0 入場／1 離開 |
| 4 | 首個 RFID byte |
| 5／6 | 晶片辨識／錯誤 frame |
| 7 | 距離狀態，value 0 無效／1 清空／2 遮擋 |
| 8 | Phase 變化，value 對應 `Visit::Phase` |
| 9 | 離開判定啟用狀態 |
| 10 | 事件結束，value 對應 `Visit::Reason`，保留終止事件容量 |

JSONL 區分有效停留時間與整筆 `elapsed_ms`，並分開保存入場／離開掃描結果。`sleep_ms`、`sleep_calls`、`sleep_errors` 只統計手動睡眠，`sleep_accounting` 明確標為 `manual_only`；不能拿零次手動睡眠推論自動淺眠失效。
