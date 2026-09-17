# Auto probe

`auto_probe` 是不寫 Google Sheets 的硬體診斷韌體，用手機網頁啟動一次低功耗／RFID 測試，完成後再由同一頁查看摘要與 raw trace。

## 行為

1. 開機建立無密碼 `LitterProbe` AP，RFID 保持關閉。
2. 手機連線後開 `http://192.168.4.1/`。
3. 按「開始低功耗測試」後，WebServer 與 Wi-Fi 關閉並開始量測。
4. 約每 100 ms 測距；RFID 平時關閉，只在入口遮擋／清空邊緣啟動最長 10 秒的 scan window。
5. 第一次有效 `<200 mm` 後保存 60 秒 raw capture；若 15 分鐘內都沒有觸發，測試以未觸發結束。
6. 測試完成後重新建立 `LitterProbe` AP，頁面顯示結果；`/raw` 提供完整 trace。

結果包含 RFID scan 次數、辨識結果、GPIO HIGH 累積時間、UART bytes／壞封包、distance samples／invalid、最大 sample gap、Light-sleep 次數與時間，以及 trace drop 計數。

`GPIO HIGH` 與 `sleep_ms` 是控制／時間診斷，不是實際電流或耗電量；續航仍需電流量測或電池實測。

## 編譯

1. 複製本資料夾的 `secrets.example.h` 為 `secrets.h`，填入兩隻貓的晶片 ID 與名稱。
2. Arduino IDE 開啟 `firmware/tests/auto_probe/auto_probe.ino`。
3. Board 選 `XIAO ESP32S3`，Verify 後 Upload 或 Export Compiled Binary。

## Web OTA

維護頁可上傳 Arduino 匯出的 `*.ino.bin` application image。成功後 ESP32 自動重啟並回到 `LitterProbe` 維護頁。

OTA 只適用於 application image；不要上傳 `merged.bin`、`bootloader.bin` 或 `partitions.bin`。目前使用的 partition scheme 必須包含 OTA data、`ota_0` 與 `ota_1`。
