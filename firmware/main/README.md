# Main firmware

`firmware/main/` 是目前正式的貓砂櫃韌體，負責 VL53L0X 入口偵測、XY-134.2K RFID 辨識、離線待傳佇列、Google Sheets 上傳、診斷資料與 Web OTA。

## 事件判定

目前參數定義在 `app_config.h`：

- 入口遮擋：有效距離嚴格小於 `200 mm`
- 待機測距週期：`200 ms`
- 事件中測距週期：`100 ms`
- 入口清空 `250 ms` 後允許下一次遮擋被視為離開
- 離開後需連續清空 `1 s`，且 RFID 掃描已結束，才完成正常事件
- 單次 RFID 掃描窗口：`10 s`
- 未確認離開：`300 s` 結束為 `no_exit_timeout`
- 已進入離開階段但未正常完成：最晚 `310 s` 結束為 `exit_unconfirmed_timeout`

流程：

1. 待機時第一次 `<200 mm` 建立暫定事件並開始 RFID 掃描。
2. 入口連續清空至少 250 ms 後，exit detection 進入 armed 狀態。
3. 下一次由清空轉為 `<200 mm` 時記為離開起點；若沒有其他 RFID scan 正在進行，會啟動離開掃描。
4. 離開後入口連續清空至少 1 秒，且 RFID scan 已結束，事件以 `normal_exit` 完成。

停留時間是第一次遮擋到離開遮擋的差值，不包含 RFID 或網路等待。只有 `normal_exit`、已辨識到本機登錄貓咪、且事件中沒有讀到互相衝突身分時才會建立 Sheet 紀錄；其他結果只保存診斷。

無效 VL53L0X sample 或超過 1 秒的 sample gap 不會被當成「持續清空」。連續 5 秒無有效距離時會重新初始化感測器；初始化失敗後以 5 秒間隔重試。

## RFID 與低功耗

RFID 的 ON/OFF 接腳為 GPIO1。只有 scan window 期間拉 HIGH；讀到已登錄晶片或 scan timeout 後立即拉 LOW。RFID 關閉且網路維護頁未啟用時，主迴圈會在測距 deadline 之間使用 Light-sleep。

## 網路、待傳佇列與維護頁

- 開機維護窗口：2 分鐘
- 每次事件結束後維護窗口：1 分鐘
- STA 連線最多等待 8 秒；失敗時建立 `LitterCabinet` AP，網址為 `http://192.168.4.1/`
- 有 Internet 的 STA 維護窗口會嘗試上傳 NVS 待傳紀錄
- NVS `litter` namespace 最多保留 8 筆 pending records；上傳成功後只推進 ring queue metadata

維護頁提供目前狀態、live log、持久診斷、raw trace 與 Web OTA。`DEBUG_WEB_SERVER=true` 時維護頁會保持開啟，適合除錯；正常部署可維持 `false`。

## 診斷資料

- RAM live log：最多 64 筆，循環覆寫
- SPIFFS `diag.jsonl`：達 64 KB 後輪替為 `diag.prev.jsonl`
- raw trace：`trace0.csv`～`trace7.csv`，8 個 slot 循環覆寫
- 每次 trace 最多 512 個距離 sample 與 48 個 RFID event
- trace 與 diagnostics 在事件結束後寫入，不在每次 100 ms 測距時同步寫 Flash

常用路徑：

- `/`：狀態與 OTA
- `/live`：本次開機的 live log
- `/diagnostics`：診斷下載入口
- `/diag/current`、`/diag/previous`：JSONL 摘要
- `/trace?slot=0`～`7`：raw trace

## Web OTA

維護頁只接受 application image，例如 Arduino 匯出的 `main.ino.bin`。事件進行中會拒絕 OTA。成功後裝置自動重啟。

OTA 需要 partition scheme 同時包含 `otadata`、`ota_0`、`ota_1` 與 SPIFFS；不要把 bootloader、partition image 或 merged image 當成 application OTA 上傳。

## 編譯與測試

1. 複製 `secrets.example.h` 為本機 `secrets.h`，設定 Wi-Fi、Apps Script URL/token 與兩隻貓的晶片 ID／名稱。
2. Arduino IDE 開啟 `firmware/main/main.ino`。
3. Board 選 `XIAO ESP32S3`，使用符合上述 OTA/SPIFFS 要求的 8 MB partition scheme。
4. Verify 後再 Upload 或 Export Compiled Binary。

State machine host regression：

```powershell
python firmware/tests/main_host/run.py
```

這些 host tests 驗證短訪問、RFID timeout、sample gap 與 invalid sample 等邏輯；真實讀距、RFID 命中率、供電與續航仍需要實機測試。
