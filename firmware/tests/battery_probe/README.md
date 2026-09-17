# Battery / RFID probe

`battery_probe` 是離線硬體診斷工具，用來比較 RFID 按需供電、Light-sleep 與 RFID 常開參考模式。它不建立正式如廁紀錄、不連 Wi-Fi、不寫 Google Sheets，也不把測試記錄中的 uptime 解讀成實際停留時間。

## 固定參數

- VL53L0X 判定門檻：`<200 mm`
- 測距週期：`100 ms`
- 單次 RFID 掃描時間：`10 s`
- `a` / `b` 最長等待入口觸發：`15 min`
- 觸發後原始記錄：`60 s`

RFID ON/OFF 使用 GPIO1，VL53L0X 使用 GPIO5/GPIO6，RFID RX 使用 GPIO44。

## 測試模式

| 指令 | 模式 | RFID / sleep | 結束條件 |
|---|---|---|---|
| `a` | gated + awake | RFID 按需開啟；CPU 不進 Light-sleep | 首次 `<200 mm` 後 60 秒，或等待 15 分鐘未觸發 |
| `b` | gated + Light-sleep | RFID 按需開啟；允許 Light-sleep | 同上；一般驗證優先使用此模式 |
| `c` | always-on reference | RFID 常開；CPU 不睡 | 60 秒 |
| `i` | idle sleep | RFID 按需；允許 Light-sleep | 60 秒 |
| `d` | dump | 讀取最後一次測試記錄 | — |
| `x` | cancel | 取消下一次已設定的模式 | — |
| `h` | help | 顯示目前設定 | — |

`a`、`b`、`c`、`i` 只會設定**下一次開機**的測試模式，不會立刻開始測試。

## 使用方式

1. 複製 `secrets.example.h` 為本機 `secrets.h`，填入兩隻貓的晶片 ID；此工具不需要網路設定。
2. Arduino IDE 開 `firmware/tests/battery_probe/battery_probe.ino`，開發板選 `XIAO ESP32S3`，Verify 後 Upload。
3. Serial Monitor 使用 115200 baud，送 `h` 確認設定。
4. 送 `i`、`b`、`a` 或 `c`，設定下一次開機的測試模式。
5. 拔除電腦 USB，再用實際電池／MT3608 供電完成測試。量測期間不輸出 Serial，也不寫 Flash。
6. 測試記錄完成後會存入獨立 NVS `battery-probe` 命名空間。接回 USB，必要時 RESET，再送 `d` 讀取。

未成功保存或 `interrupted_run` 非零時，不應把舊記錄當成本次測試結果。

## 輸出解讀

摘要欄位：

- `commanded_on_ms`：GPIO1 被命令為 HIGH 的累積時間，不等於實際電流或電量。
- `sleep_calls / sleep_ms / sleep_errors`：Light-sleep 成功次數、累積時間與錯誤數。
- `samples / invalid / max_gap_ms`：VL53L0X 取樣品質。
- `scans / recognized / other_valid`：RFID 掃描次數、已登錄晶片命中與其他有效晶片封包。
- `bytes / bad_frames / stale_bytes`：UART 資料品質。
- `sample_drops / event_drops / sensor_failed / aborted`：測試記錄完整性；非零值需要先排除再解讀結果。

原始追蹤資料：

```text
S,uptime_ms,distance_mm,flags
E,uptime_ms,code,value,aux
```

`S.flags`：bit0=有效取樣、bit1=`<200 mm`、bit2=該次取樣時 GPIO1 為 HIGH。所有時間都是本次開機 uptime，不是實際時刻。

事件代碼：1/2=RFID power on/off、3=scan start、4=first UART byte、5=valid chip、6=bad frame、7=scan end、8=sensor error、9=stale bytes、10=sleep error。

## 主機端測試

從專案根目錄執行：

```sh
python firmware/tests/battery_probe/run_host_tests.py
```

主機端測試只驗證 `probe_logic.h` 邏輯；RFID 冷啟動、實際電流與植入晶片讀取率仍需要硬體測試。
