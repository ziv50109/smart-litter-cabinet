# Auto probe

`auto_probe` 是不寫 Google Sheets 的硬體診斷韌體，用手機網頁啟動一次低功耗／RFID 測試，完成後再由同一頁查看摘要與原始追蹤資料。

## 參數來源

測距門檻、測距週期、RFID 掃描逾時、觸發後記錄時間與等待觸發期限皆定義在同資料夾的 `probe_logic.h`：

- `Probe::THRESHOLD_MM`
- `Probe::SAMPLE_MS`
- `Probe::SCAN_MS`
- `Probe::CAPTURE_MS`
- `Probe::WAIT_MS`

README 不重複這些參數的目前數值。

## 行為

1. 開機建立無密碼 `LitterProbe` AP，RFID 保持關閉。
2. 手機連線後開 `http://192.168.4.1/`。
3. 按「開始低功耗測試」後，Web server 與 Wi-Fi 關閉並開始量測。
4. 依 `Probe::SAMPLE_MS` 測距；RFID 平時關閉，只在入口遮擋狀態改變時啟動掃描，並由 `Probe::SCAN_MS` 限制單次掃描時間。
5. 第一次有效距離低於 `Probe::THRESHOLD_MM` 後開始保存原始記錄；記錄長度與等待觸發期限分別由 `Probe::CAPTURE_MS`、`Probe::WAIT_MS` 控制。
6. 測試完成後重新建立 `LitterProbe` AP，頁面顯示結果；`/raw` 提供完整原始追蹤資料。

結果包含 RFID 掃描次數、辨識結果、GPIO HIGH 累積時間、UART 位元組數／壞封包、測距取樣數／無效取樣數、最大取樣間隔、Light-sleep 次數與時間，以及追蹤資料遺失計數。

`GPIO HIGH` 與 `sleep_ms` 是控制／時間診斷，不是實際電流或耗電量；續航仍需電流量測或電池實測。

## 編譯

1. 複製本資料夾的 `secrets.example.h` 為 `secrets.h`，填入兩隻貓的晶片 ID 與名稱。
2. Arduino IDE 開啟 `firmware/tests/auto_probe/auto_probe.ino`。
3. 開發板選 `XIAO ESP32S3`，Verify 後 Upload 或 Export Compiled Binary。

## Web OTA

維護頁可上傳 Arduino 匯出的 `*.ino.bin` 應用程式映像檔。成功後 ESP32 自動重啟並回到 `LitterProbe` 維護頁。

OTA 只適用於應用程式映像檔；不要上傳 `merged.bin`、`bootloader.bin` 或 `partitions.bin`。使用的分割區配置必須支援 OTA。
