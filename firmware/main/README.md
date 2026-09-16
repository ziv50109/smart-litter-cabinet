# Main firmware — 低功耗正式候選版

這版直接取代原始 `firmware/main/` 的高耗電行為，同時保留 Google Sheets、Web Debug、長期診斷資料與 Web OTA。

## 已改善

- RFID 平時 GPIO1=LOW；只有掃描窗口才 HIGH。讀到已登錄晶片或 10 秒逾時後立即關閉。
- Idle 測距 200ms；事件期間 100ms。
- Wi-Fi 平時關閉；開機提供 2 分鐘維護窗口，事件結束提供 1 分鐘維護窗口。
- 優先以 STA 連家中 Wi-Fi。若連不上，才開 `LitterCabinet` AP，網址 `http://192.168.4.1/`。
- Web 頁面提供即時狀態、live log、持久診斷檔與 Web OTA。
- 短訪問不再要求先 clear 10 秒才允許離開：有效 clear 約 250ms 後再次 `<200mm` 即形成 exit candidate；離開後 clear 1 秒且 RFID 掃描完成才結案。Duration 採第二次通過開始時間，不包含 RFID/網路等待。
- `no_exit_timeout` / `exit_unconfirmed_timeout` 只保存診斷，不上傳 Sheets，避免把固定 300 秒冒充真實停留時間。
- 正常事件維持原 Sheets 九欄格式。

## Merge 前健壯性

- VL53L0X 連續 5 秒無效時自動重新初始化；失敗後以 5 秒 backoff 重試，不需人工重開機。
- clear / exit 判定只接受連續觀測；樣本間隔超過 1 秒或遇到 invalid sample 時，未觀測的時間不算 clear 證據。
- NVS 待傳紀錄沿用 `litter` namespace 與單一 checksum metadata 的 ring queue。先寫 payload、再發布 count；成功上傳後只原子推進 head/count，不搬移整個 queue，降低突然斷電造成遺失或重排的風險。
- diagnostics SPIFFS 以 `formatOnFail` 掛載；首次未格式化或診斷 filesystem 無法掛載時只重建 diagnostics partition，不影響 OTA app slot 或 NVS pending queue。
- OTA 在活動中的貓咪事件期間會拒絕更新，避免更新流程中斷 RFID / distance event。

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
```

本機既有 `firmware/main/secrets.h` 會直接沿用，不需要搬到別的資料夾。

Arduino IDE 開：

```text
firmware/main/main.ino
```

板子選 `XIAO ESP32S3`，使用目前 8MB、含 `otadata` / `ota_0` / `ota_1` / SPIFFS 的 partition scheme。先 Verify；通過後 Export Compiled Binary。

純 state machine host regression：

```powershell
python firmware/tests/main_host/run.py
```

涵蓋短訪問、exit scan timeout、不持續遮擋誤判、長 sample gap 與 invalid sample 不得被當成連續 clear。

## 若目前裝置已有 Web OTA

如果現在運行中的維護頁已看得到「無線更新韌體 / Web OTA」，手機或電腦可以直接選新的：

```text
main.ino.bin
```

上傳更新，不需要 USB。事件正在進行時 OTA 會被拒絕，等事件結束後的維護窗口再更新即可。

如果目前頁面沒有 OTA 區塊，才需要最後一次手機 USB/OTG。

## 最後一次手機 USB 燒錄

先查看該次 build 的 `flash_args`。若 application 仍為：

```text
0x10000 main.ino.bin
```

把 `main.ino.bin` 放到手機後：

```bash
nrflash write --chip esp32s3 --offset 0x10000 /storage/emulated/0/Download/ESP32_debug/main.ino.bin --verify
```

不要 Erase，也不要刷 merged / bootloader / partitions。

刷完後拔手機 USB、接回 MT3608。

## 之後 OTA

開機後前 2 分鐘會嘗試連家裡 Wi-Fi：

- 成功：ESP32 與手機/電腦在同一 LAN，可用 ESP32 的 DHCP IP 開維護頁；ESP32 同時可正常上網。
- 失敗：會出現 `LitterCabinet` Wi-Fi，連上後開 `http://192.168.4.1/`。

維護頁的 Web OTA 只上傳 Arduino 匯出的 `main.ino.bin` / 後續 `*.ino.bin` application image。成功後自動重啟；之後正常更新不再需要 USB、Termux 或 BOOT。

## 實機驗收

目前 probe 已實測空櫃 RFID 不開啟、Light-sleep 正常；真貓通過曾得到兩次按需 RFID 掃描且兩次辨識成功。正式 `main` 仍應持續觀察：

1. 待機時 `rfid_on=false`。
2. 貓通過時 RFID 才 ON，且兩隻貓均能穩定辨識。
3. Sheets duration 與實際短訪問接近，不再出現假的 241/300 秒。
4. `/diagnostics` 可下載事件摘要與 raw trace。
5. Web OTA 能在兩個 OTA slot 間更新並正常重啟。
6. 關閉維護窗口後 Wi-Fi OFF、RFID OFF，續航明顯高於原始 main。
