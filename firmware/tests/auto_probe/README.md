# Auto probe — 手機友善零 Serial 驗證版

目的：避免 USB Serial / 指令 / NVS arm 流程。這是診斷版，不會寫 Google Sheets。

## 行為

1. 開機先建立 `LitterProbe` Wi-Fi 與 WebServer，不會立刻開始測試；RFID 維持 LOW。
2. 手機連 `LitterProbe`，開 `http://192.168.4.1/`。頁面可直接開始測試或 Web OTA 更新。
3. 按「開始低功耗測試」後，WebServer / Wi-Fi 關閉，才開始真正的待機量測。
4. 測試期間約每 100ms 測距，平時 RFID GPIO1 維持 LOW，ESP32 在可行時 Light-sleep。
5. 首次有效 `<200mm` 後開始 60 秒 raw capture；遮擋邊緣才開 RFID，取得已登錄晶片或 10 秒逾時即關閉。
6. 若 15 分鐘內都沒有 `<200mm`，以「未觸發」結束，仍可查看待機統計。
7. 測試完成後 `LitterProbe` 與 WebServer 自動重新出現，頁面直接顯示結果。
8. Web OTA 成功後 ESP32 自動重啟，重新回到維護頁。

## 編譯

在分支 `diagnostics/battery-rfid-20260916`：

```powershell
git pull
Copy-Item firmware/main/secrets.h firmware/tests/auto_probe/secrets.h
```

Arduino IDE 開 `firmware/tests/auto_probe/auto_probe.ino`，板子選 XIAO ESP32-S3，Export Compiled Binary。

## 最後一次手機 USB 燒錄

只刷 application image；實際 offset 以該次 build 的 `flash_args` 為準。若仍是：

```text
0x10000 auto_probe.ino.bin
```

則 Termux：

```bash
nrflash write --chip esp32s3 --offset 0x10000 /storage/emulated/0/Download/ESP32_debug/auto_probe.ino.bin --verify
```

不要 Erase，不刷 merged / bootloader / partition image。

## 之後的操作

燒錄完成後拔手機 USB，接回 MT3608 電池：

1. 手機 Wi-Fi 連 `LitterProbe`（無密碼）。
2. 開 `http://192.168.4.1/`。
3. 要測試就按「開始低功耗測試」。Wi-Fi 會消失，這是正常的。
4. 第一次 `<200mm` 後約 60 秒，或無觸發等待 15 分鐘後，`LitterProbe` 會重新出現。
5. 重新連線後打開同一頁看摘要；`/raw` 看完整 trace。

結果頁顯示：是否觸發、辨識貓咪、掃描窗口、RFID GPIO HIGH 累積時間、UART bytes / 壞封包、測距 samples / invalid、最大取樣間隔、Light-sleep 次數/時間/錯誤及 trace drops。

## OTA 更新

維護頁隨時可選 Arduino 匯出的 `*.ino.bin` application image，按「上傳並更新」。成功後 ESP32 自動重啟。

只上傳 application image；不要上傳 `merged.bin`、`bootloader.bin` 或 `partitions.bin`。目前 partition table 已有 OTA data、`ota_0`、`ota_1`，因此 application OTA 可用。

這些是控制與時間診斷，不等於實際 mA。要確認真正續航仍需電流或電池續航實測。
