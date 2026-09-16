# main_v2 — 低功耗正式候選版

這版用來取代原始 `firmware/main/` 的高耗電行為，同時保留 Google Sheets、Web Debug 與 OTA。先以候選版實機驗證，確認數天資料後再收斂回正式 `main`。

## 已改善

- RFID 平時 GPIO1=LOW；只有掃描窗口才 HIGH。讀到已登錄晶片或 10 秒逾時後立即關閉。
- Idle 測距 200ms；事件期間 100ms。
- Wi-Fi 平時關閉；開機提供 2 分鐘維護窗口，事件結束提供 1 分鐘維護窗口。
- 優先以 STA 連家中 Wi-Fi。若連不上，才開 `LitterCabinet` AP，網址 `http://192.168.4.1/`。
- Web 頁面提供即時狀態、live log、持久診斷檔與 Web OTA。
- 短訪問不再要求先 clear 10 秒才允許離開：有效 clear 約 250ms 後再次 `<200mm` 即形成 exit candidate；離開後 clear 1 秒且 RFID 掃描完成才結案。Duration 採第二次通過開始時間，不包含 RFID/網路等待。
- `no_exit_timeout` / `exit_unconfirmed_timeout` 只保存診斷，不上傳 Sheets，避免把固定 300 秒冒充真實停留時間。
- 正常事件維持原 Sheets 九欄格式。

## 長期診斷資料

不需要常駐 Web Debug 才能蒐集資料：

- RAM live log 固定 64 筆，滿了覆寫最舊，不會無限長大。
- SPIFFS `diag.jsonl` 到 64KB 自動輪替成 `diag.prev.jsonl`，最多兩份摘要檔。
- 最近 8 次 raw trace 使用 `trace0.csv`～`trace7.csv` 固定循環覆寫。
- raw trace 每次最多 512 個 sample（Active 100ms 時約最近 51 秒）及 48 個重要 RFID event。
- 平時不每 100ms 寫 Flash；事件結束才一次保存，因此不把測距 loop 綁在 flash write 上。

Web 維護頁：

- `/`：即時狀態與 OTA
- `/live`：本次開機最近 live log
- `/diagnostics`：持久診斷下載入口
- `/diag/current`、`/diag/previous`：JSONL 摘要
- `/trace?slot=0`～`7`：最近 raw trace

## 編譯

在 PR branch：

```powershell
git pull
Copy-Item firmware/main/secrets.h firmware/main_v2/secrets.h
```

Arduino IDE 開：

```text
firmware/main_v2/main_v2.ino
```

板子選 `XIAO ESP32S3`，使用與目前正式版相同 partition scheme（8MB、含 ota_0 / ota_1 / SPIFFS）。先 Verify；通過後 Export Compiled Binary。

## 最後一次手機 USB 燒錄

先查看該次 build 的 `flash_args`。若 application 仍為：

```text
0x10000 main_v2.ino.bin
```

把 `main_v2.ino.bin` 放到手機後：

```bash
nrflash write --chip esp32s3 --offset 0x10000 /storage/emulated/0/Download/ESP32_debug/main_v2.ino.bin --verify
```

不要 Erase，也不要刷 merged / bootloader / partitions。

刷完後拔手機 USB、接回 MT3608。

## 之後 OTA

開機後前 2 分鐘會嘗試連家裡 Wi-Fi：

- 成功：ESP32 與手機/電腦在同一 LAN，可用 ESP32 的 DHCP IP 開維護頁。
- 失敗：會出現 `LitterCabinet` Wi-Fi，連上後開 `http://192.168.4.1/`。

維護頁的 Web OTA 只上傳 Arduino 匯出的 `main_v2.ino.bin` / 後續 `*.ino.bin` application image。成功後自動重啟；之後正常更新不再需要 USB、Termux 或 BOOT。

## 實機驗收

這版尚需在實際 XIAO ESP32-S3 Arduino toolchain Verify。上板後先確認：

1. 待機時 `rfid_on=false`。
2. 貓通過時 RFID 才 ON，且能穩定辨識兩隻貓。
3. Sheets duration 與實際短訪問接近，不再出現假的 241/300 秒。
4. `/diagnostics` 可下載事件摘要與 raw trace。
5. Web OTA 能從 `ota_0` 更新到另一 OTA slot 並正常重啟。
6. 關閉維護窗口後 Wi-Fi OFF、RFID OFF，續航明顯高於原始 main。
