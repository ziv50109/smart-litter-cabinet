# Auto probe — 手機友善零 Serial 驗證版

目的：避免 USB Serial / 指令 / NVS arm 流程。這是診斷版，不會寫 Google Sheets。

## 行為

1. 開機立即開始測試；測試期間 Wi-Fi 關閉。
2. 約每 100ms 測距，平時 RFID GPIO1 維持 LOW，ESP32 在可行時 Light-sleep。
3. 首次有效 `<200mm` 後開始 60 秒 raw capture；遮擋邊緣才開 RFID，取得已登錄晶片或 10 秒逾時即關閉。
4. 若 15 分鐘內都沒有 `<200mm`，以「未觸發」結束，仍可查看待機統計。
5. 測試結束後才建立開放 Wi-Fi `LitterProbe`。手機連上後開 `http://192.168.4.1/` 看摘要；`/raw` 看完整 trace。
6. 網頁「重新測一次」會重啟，Wi-Fi 立即消失，重新進入測試；完成後 `LitterProbe` 再出現。

## 編譯

在分支 `diagnostics/battery-rfid-20260916`：

```powershell
git pull
Copy-Item firmware/main/secrets.h firmware/tests/auto_probe/secrets.h
```

Arduino IDE 開 `firmware/tests/auto_probe/auto_probe.ino`，板子選 XIAO ESP32-S3，Export Compiled Binary。

## 手機燒錄

只刷 application image；實際 offset 以該次 build 的 `flash_args` 為準。若仍是：

```text
0x10000 auto_probe.ino.bin
```

則 Termux：

```bash
nrflash write --chip esp32s3 --offset 0x10000 /storage/emulated/0/Download/ESP32_debug/auto_probe.ino.bin --verify
```

不要 Erase，不刷 merged / bootloader / partition image。

## 測試

燒錄完成後拔手機 USB，再接回 MT3608 電池。不要做任何 Serial 操作。

- 讓裝置待機。
- 15 分鐘內讓任一隻已登錄貓正常通過入口一次。
- 第一次 `<200mm` 後約 60 秒，手機 Wi-Fi 清單應出現 `LitterProbe`。
- 連上 `LitterProbe`（無密碼），手機可能提示「無網際網路」，選擇保持連線。
- 開 `http://192.168.4.1/`。

結果頁顯示：是否觸發、辨識貓咪、掃描窗口、RFID GPIO HIGH 累積時間、UART bytes / 壞封包、測距 samples / invalid、最大取樣間隔、Light-sleep 次數/時間/錯誤及 trace drops。

這些是控制與時間診斷，不等於實際 mA。要確認真正續航仍需電流或電池續航實測。
