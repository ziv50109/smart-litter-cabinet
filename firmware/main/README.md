# Main firmware

`firmware/main/` 是目前正式的貓砂櫃韌體，負責 VL53L0X 入口偵測、XY-134.2K RFID 辨識、離線待傳佇列、Google Sheets 上傳、診斷資料與 Web OTA。

## 事件判定

所有可調門檻與時間參數以 `app_config.h` 為唯一來源，README 不重複目前數值：

| 行為 | 設定 |
|---|---|
| 入口遮擋門檻 | `Config::ENTRY_THRESHOLD_MM` |
| 待機／事件中測距週期 | `Config::IDLE_RANGING_PERIOD_MS` / `Config::ACTIVE_RANGING_PERIOD_MS` |
| 連續觀測允許的取樣間隔 | `Config::SAMPLE_GAP_MS` |
| 允許判定離開所需的無遮擋時間 | `Config::EXIT_ARM_CLEAR_MS` |
| 離開後完成事件所需的無遮擋時間 | `Config::EXIT_CLEAR_CONFIRM_MS` |
| RFID 掃描逾時 | `Config::RFID_TIMEOUT_MS` |
| 未確認離開逾時 | `Config::MAX_SESSION_DURATION_MS` |
| 離開階段最終期限 | `Config::EXIT_FINAL_DEADLINE_MS` |
| ToF 無效量測重啟／重試 | `Config::TOF_INVALID_RESTART_MS` / `Config::TOF_REINIT_BACKOFF_MS` |

流程：

1. 待機時有效距離低於 `ENTRY_THRESHOLD_MM`，建立暫定事件並開始 RFID 掃描。
2. 入口持續無遮擋達 `EXIT_ARM_CLEAR_MS` 後，允許下一次遮擋建立離開候選。
3. 下一次由無遮擋轉為遮擋時建立離開候選並記錄離開起點；若沒有其他 RFID 掃描正在進行，會啟動離開掃描。
4. 建立離開候選後，入口持續無遮擋達 `EXIT_CLEAR_CONFIRM_MS` 且 RFID 掃描已結束，事件以 `normal_exit` 完成。

停留時間是第一次入口遮擋與建立離開候選的第二次遮擋之間的時間差，不包含 RFID 或網路等待。只有 `normal_exit`、已辨識到本機登錄貓咪，且事件中沒有讀到互相衝突的身分時才會建立 Sheet 紀錄；其他結果僅保留診斷資料。

無效 VL53L0X 取樣或取樣間隔超過 `SAMPLE_GAP_MS` 時，未觀測期間不會被當成持續無遮擋。感測器重啟與重試時機分別由 `TOF_INVALID_RESTART_MS`、`TOF_REINIT_BACKOFF_MS` 控制。

## RFID 與低功耗

RFID 的啟用接腳由 `Config::RFID_ENABLE_PIN` 定義。只有 RFID 掃描期間會啟用；讀到已登錄晶片或掃描逾時後立即關閉。RFID 關閉且網路維護頁未啟用時，主迴圈會利用下一次測距前的空檔進入 Light-sleep。

## 網路、待傳佇列與維護頁

相關容量與時間設定同樣以 `app_config.h` 為準：

| 行為 | 設定 |
|---|---|
| STA 連線逾時 | `Config::WIFI_TIMEOUT_MS` |
| NTP 同步逾時 | `Config::NTP_TIMEOUT_MS` |
| 開機維護模式 | `Config::BOOT_MAINTENANCE_MS` |
| 事件後維護模式 | `Config::POST_EVENT_MAINTENANCE_MS` |
| NVS 待傳紀錄上限 | `Config::MAX_PENDING_RECORDS` |
| 原始追蹤資料槽位數 | `Config::TRACE_SLOTS` |
| 診斷檔輪替門檻 | `Config::DIAG_ROTATE_BYTES` |

STA 可連上網際網路時，會在維護模式中嘗試上傳 NVS 待傳紀錄；上傳成功後只更新環形佇列的索引資訊。STA 連線失敗時會建立 `LitterCabinet` AP。

維護頁提供目前狀態、即時紀錄、持久診斷資料、原始追蹤資料與 Web OTA。`DEBUG_WEB_SERVER=true` 時維護頁會保持開啟，適合除錯；正常部署可維持 `false`。

## 診斷資料

- RAM 即時紀錄採固定容量循環覆寫，不會無限成長。
- SPIFFS `diag.jsonl` 達 `Config::DIAG_ROTATE_BYTES` 後輪替為 `diag.prev.jsonl`。
- 原始追蹤資料使用 `Config::TRACE_SLOTS` 個槽位循環覆寫。
- 每份追蹤資料的取樣與 RFID 事件數都有固定上限。
- 追蹤資料與診斷資料在事件結束後寫入，不會在每次測距時同步寫入 Flash。

常用路徑：

- `/`：狀態與 OTA
- `/live`：本次開機的即時紀錄
- `/diagnostics`：診斷下載入口
- `/diag/current`、`/diag/previous`：JSONL 摘要
- `/trace?slot=<n>`：原始追蹤資料

## Web OTA

維護頁只接受應用程式映像檔，例如 Arduino 匯出的 `main.ino.bin`。事件進行中會拒絕 OTA。成功後裝置自動重啟。

OTA 使用的分割區配置必須同時包含 `otadata`、`ota_0`、`ota_1` 與 SPIFFS；不要將 bootloader、分割區映像檔或 merged image 當成應用程式映像檔上傳。

## 編譯與測試

1. 複製 `secrets.example.h` 為本機 `secrets.h`，設定 Wi-Fi、Apps Script URL/token 與兩隻貓的晶片 ID／名稱。
2. Arduino IDE 開啟 `firmware/main/main.ino`。
3. 開發板選 `XIAO ESP32S3`，使用符合上述 OTA/SPIFFS 要求的分割區配置。
4. Verify 後再 Upload 或 Export Compiled Binary。

狀態機主機端回歸測試：

```sh
python firmware/tests/main_host/run.py
```

這些主機端測試驗證短事件、RFID 逾時、取樣間隔與無效取樣等邏輯；真實讀距、RFID 命中率、供電與續航仍需要實機測試。
