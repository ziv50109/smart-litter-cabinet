# Main firmware

`firmware/main/` 是目前正式的貓砂櫃韌體，負責 VL53L0X 入口偵測、XY-134.2K RFID 辨識、離線待傳佇列、Google Sheets 上傳、診斷資料與 Web OTA。

## 事件判定

目前參數定義在 `app_config.h`：

- 入口遮擋：有效距離嚴格小於 `200 mm`
- 待機測距週期：`200 ms`
- 事件中測距週期：`100 ms`
- 入口持續無遮擋 `250 ms` 後，允許下一次遮擋建立離開候選
- 建立離開候選後，入口需持續無遮擋 `1 s`，且 RFID 掃描已結束，才完成正常事件
- 單次 RFID 掃描時間：`10 s`
- 未確認離開：`300 s` 結束為 `no_exit_timeout`
- 已進入離開階段但未正常完成：最晚 `310 s` 結束為 `exit_unconfirmed_timeout`

流程：

1. 待機時第一次 `<200 mm` 建立暫定事件並開始 RFID 掃描。
2. 入口持續無遮擋至少 250 ms 後，允許判定下一次遮擋為離開候選。
3. 下一次由無遮擋轉為 `<200 mm` 時建立離開候選並記錄離開起點；若沒有其他 RFID 掃描正在進行，會啟動離開掃描。
4. 建立離開候選後，入口持續無遮擋至少 1 秒且 RFID 掃描已結束，事件以 `normal_exit` 完成。

停留時間是第一次入口遮擋與建立離開候選的第二次遮擋之間的時間差，不包含 RFID 或網路等待。只有 `normal_exit`、已辨識到本機登錄貓咪，且事件中沒有讀到互相衝突的身分時才會建立 Sheet 紀錄；其他結果僅保留診斷資料。

無效 VL53L0X 取樣或相鄰取樣間隔超過 1 秒時，未觀測期間不會被當成「持續無遮擋」。連續 5 秒無有效距離時會重新初始化感測器；初始化失敗後以 5 秒間隔重試。

## RFID 與低功耗

RFID 的 ON/OFF 接腳為 GPIO1。只有 RFID 掃描期間會拉 HIGH；讀到已登錄晶片或掃描逾時後立即拉 LOW。RFID 關閉且網路維護頁未啟用時，主迴圈會利用下一次測距前的空檔進入 Light-sleep。

## 網路、待傳佇列與維護頁

- 開機後維護模式：2 分鐘
- 每次事件結束後維護模式：1 分鐘
- STA 連線最多等待 8 秒；失敗時建立 `LitterCabinet` AP，網址為 `http://192.168.4.1/`
- STA 可連上網際網路時，會在維護模式中嘗試上傳 NVS 待傳紀錄
- NVS `litter` 命名空間最多保留 8 筆待傳紀錄；上傳成功後只更新環形佇列的索引資訊

維護頁提供目前狀態、即時紀錄、持久診斷資料、原始追蹤資料與 Web OTA。`DEBUG_WEB_SERVER=true` 時維護頁會保持開啟，適合除錯；正常部署可維持 `false`。

## 診斷資料

- RAM 即時紀錄：最多 64 筆，循環覆寫
- SPIFFS `diag.jsonl`：達 64 KB 後輪替為 `diag.prev.jsonl`
- 原始追蹤資料：`trace0.csv`～`trace7.csv`，8 個檔案循環覆寫
- 每份追蹤資料最多 512 個距離取樣與 48 個 RFID 事件
- 追蹤資料與診斷資料在事件結束後寫入，不會每 100 ms 測距一次就同步寫入 Flash

常用路徑：

- `/`：狀態與 OTA
- `/live`：本次開機的即時紀錄
- `/diagnostics`：診斷下載入口
- `/diag/current`、`/diag/previous`：JSONL 摘要
- `/trace?slot=0`～`7`：原始追蹤資料

## Web OTA

維護頁只接受應用程式映像檔，例如 Arduino 匯出的 `main.ino.bin`。事件進行中會拒絕 OTA。成功後裝置自動重啟。

OTA 使用的分割區配置必須同時包含 `otadata`、`ota_0`、`ota_1` 與 SPIFFS；不要將 bootloader、分割區映像檔或 merged image 當成應用程式映像檔上傳。

## 編譯與測試

1. 複製 `secrets.example.h` 為本機 `secrets.h`，設定 Wi-Fi、Apps Script URL/token 與兩隻貓的晶片 ID／名稱。
2. Arduino IDE 開啟 `firmware/main/main.ino`。
3. 開發板選 `XIAO ESP32S3`，使用符合上述 OTA/SPIFFS 要求的 8 MB 分割區配置。
4. Verify 後再 Upload 或 Export Compiled Binary。

狀態機主機端回歸測試：

```sh
python firmware/tests/main_host/run.py
```

這些主機端測試驗證短事件、RFID 逾時、取樣間隔與無效取樣等邏輯；真實讀距、RFID 命中率、供電與續航仍需要實機測試。
