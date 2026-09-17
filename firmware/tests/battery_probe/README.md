# Battery / RFID probe

`battery_probe` 是離線硬體診斷工具，用來比較 RFID 按需供電、Light-sleep 與 RFID 常開參考模式。它不建立正式如廁紀錄、不連 Wi-Fi、不寫 Google Sheets，也不把 capture uptime 解讀成實際停留時間。

## 固定參數

- VL53L0X threshold：`<200 mm`
- 測距週期：`100 ms`
- 單次 RFID scan window：`10 s`
- `a` / `b` 最長等待入口觸發：`15 min`
- 觸發後 raw capture：`60 s`

RFID ON/OFF 使用 GPIO1，VL53L0X 使用 GPIO5/GPIO6，RFID RX 使用 GPIO44。

## 測試模式

| 指令 | 模式 | RFID / sleep | 結束條件 |
|---|---|---|---|
| `a` | gated + awake | RFID 按需開啟；CPU 不進 Light-sleep | 首次 `<200 mm` 後 60 秒，或等待 15 分鐘未觸發 |
| `b` | gated + Light-sleep | RFID 按需開啟；允許 Light-sleep | 同上；一般驗證優先使用此模式 |
| `c` | always-on reference | RFID 常開；CPU 不睡 | 60 秒 |
| `i` | idle sleep | RFID 按需；允許 Light-sleep | 60 秒 |
| `d` | dump | 讀取最後一次 capture | — |
| `x` | cancel | 取消下一次已 arm 的模式 | — |
| `h` | help | 顯示目前設定 | — |

`a`、`b`、`c`、`i` 只會 arm **下一次開機**，不會立刻開始測試。

## 使用方式

1. 複製 `secrets.example.h` 為本機 `secrets.h`，填入兩隻貓的晶片 ID；此工具不需要網路設定。
2. Arduino IDE 開 `firmware/tests/battery_probe/battery_probe.ino`，Board 選 `XIAO ESP32S3`，Verify 後 Upload。
3. Serial Monitor 使用 115200 baud，送 `h` 確認設定。
4. 送 `i`、`b`、`a` 或 `c` arm 下一次開機。
5. 拔除電腦 USB，再用實際電池／MT3608 供電完成測試。量測期間不輸出 Serial，也不寫 Flash。
6. Capture 完成後會存入獨立 NVS `battery-probe` namespace。接回 USB，必要時 RESET，再送 `d` 讀取。

未成功保存或 `interrupted_run` 非零時，不應把舊 capture 當成本次測試結果。

## 輸出解讀

摘要欄位：

- `commanded_on_ms`：GPIO1 被命令為 HIGH 的累積時間，不等於實際電流或電量。
- `sleep_calls / sleep_ms / sleep_errors`：Light-sleep 成功次數、累積時間與錯誤數。
- `samples / invalid / max_gap_ms`：VL53L0X 取樣品質。
- `scans / recognized / other_valid`：scan window、已登錄晶片命中與其他有效晶片封包。
- `bytes / bad_frames / stale_bytes`：UART 資料品質。
- `sample_drops / event_drops / sensor_failed / aborted`：capture 完整性；非零值需要先排除再解讀結果。

Raw trace 中：

```text
S,uptime_ms,distance_mm,flags
E,uptime_ms,code,value,aux
```

`S.flags`：bit0=有效 sample、bit1=`<200 mm`、bit2=該 sample 時 GPIO1 為 HIGH。所有時間都是本次開機 uptime，不是牆上時間。

Event code：1/2=RFID power on/off、3=scan start、4=first UART byte、5=valid chip、6=bad frame、7=scan end、8=sensor error、9=stale bytes、10=sleep error。

## Host test

從 repository root 執行：

```powershell
python firmware/tests/battery_probe/run_host_tests.py
```

Host test 只驗證 probe logic；RFID 冷啟動、實際電流與植入晶片讀取率仍需要硬體測試。
