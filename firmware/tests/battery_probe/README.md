# Battery / cold-start RFID verification — 驗證版，不是正式修復

**Status: DRAFT / hardware acceptance pending. Main firmware and Sheets are unchanged.**

這支獨立 sketch 是針對「1000mAh 約 6 小時耗光、實際不到 30 秒卻記成 241／300 秒、RFID 啟停曾讀不到」的診斷版本。先驗證 **空櫃時 RFID 真的收到關閉指令、主控實際進入 Light-sleep、短暫通過仍取得晶片**，不把「一直開著所以讀到了」當修好。

它不建立如廁紀錄、不猜進出方向、不連網、不寫 Google Sheets。燒錄它會暫時取代正式記錄功能；結束驗證後須重新燒錄 `firmware/main/main.ino` 才會恢復正式功能，但目前 main 仍有原本問題。不要把本 PR 合併或主機測試通過當成正式省電修復已完成。

## 已確認的程式問題與仍未知的事

基線：`996aa7be99b8ecafeaec1d53ad041cbce05dd840`。

* Main 在 setup 常開 RFID，掃描窗口只篩選 UART 資料，沒有控制模組關閉。
* 主機測試使用原始 `firmware/main/visit_logic.h`，以 100ms 觀測重播：1s 入口遮擋、2s 清空、7s 再遮擋、8s 清空，1.1s 已讀到 ID。Main 未曾累積足夠的 10s 清空以進入 Inside，最後輸出 300s 的 `no_exit_timeout`。這是已重現的限制，不是修復成功測試。
* 1s／20s 遮擋的對照可正常得到 19s；因此不能說所有 <30s 事件都一定有同一成因，更不能單憑現有 Sheets 證明那筆 241s 的原因。
* 單一入口感測器的「清空」不等於已離開櫃子。沒有離開證據時，未來正式版本應明確標記不確定／逾時，不應把固定 300s 當實際停留時間。本 PR 不擅自更改已核准的事件規則或後端 schema。

## 使用：先 i 測待機，再 b 測實際通過

接線不變：RFID ON/OFF=GPIO1、RX=GPIO44、TX 不配置；VL53L0X SDA=GPIO5、SCL=GPIO6。維持 `<200mm` 遮擋、`>=200mm` 清空，**205mm 不視為貓**。測距間隔仍為 100ms，沒有為省電偷偷拉到 5 秒。

1. 在此分支的 repo 根目錄，將原本本機的設定複製過來：

   ```powershell
   Copy-Item firmware/main/secrets.h firmware/tests/battery_probe/secrets.h
   ```

   或複製本資料夾 `secrets.example.h` 為 `secrets.h`，只填兩隻貓的晶片 ID。檔案已由既有 `.gitignore` 排除。空白註冊表只能看到 `other_valid`，不會被當成成功辨識。不要提交或分享 secrets.h。

2. Arduino IDE 開啟 **`firmware/tests/battery_probe/battery_probe.ino`**，選 XIAO ESP32-S3、既有 ESP32 平台與 Pololu VL53L0X，啟用 USB CDC On Boot。先 Verify，再 Upload。此環境尚未完成真正的 ESP32 平台編譯，須先過這一關。
3. 序列埠監控視窗 115200 baud，送 `h` 查看模式與 `registered cats=2`。送 `i`，必須看到 `ARMED mode=4`。指令只預約**下一次開機**，不立刻執行。
4. **先拔電腦 USB，再接 MT3608 電池供電**。空櫃保持 60 秒，結束後結果存 NVS；留出保存時間，再拔電池供電並接回 USB。開監控視窗送 `d`；沒有反應可按 RESET 後再送 `d`。不要按著 BOOT 上電。
5. 待機結果正常後，送 `b` 預約下一次開機，再以同樣方式換成電池。它最多等待 15 分鐘；第一次 `<200mm` 後保存 60 秒原始軌跡，涵蓋這次短進出。整段測試不開 Wi-Fi。等貓離開且 60 秒捕捉完成後，接回 USB 送 `d`。保持天線位置不變並以實際植入晶片的貓測試，不強迫貓通過。
6. 複製 `BEGIN_CAPTURE` 到 `END_CAPTURE` 的全部內容存為 `.txt`，並記下人眼／影片看到的大約進出秒數與當時電流。dump 不含完整晶片 ID，`cat=1/2` 對應本機註冊順序。

> ARM 命令只消耗一次。正常取回結果的重啟不會再啟動測試，也不會自動覆寫上次結果。初次配置／結果讀取畫面不是電池待機測量狀態，電流要在已啟動的 i/b 測試期間量。

| 指令 | 下一次開機的模式 | RFID 與睡眠 | 結束條件 |
|---|---|---|---|
| `i` | 空櫃待機檢查 | 按需 RFID、允許 Light-sleep；意外遮擋仍會掃描 | 開始後 60 秒 |
| `b` | 主要電池掃描驗證 | 按需 RFID、允許 Light-sleep | 首次遮擋後 60 秒，或等待 15 分鐘沒有遮擋 |
| `a` | 隔離睡眠影響 | 按需 RFID、CPU 不睡；其他掃描邏輯相同 | 同 b |
| `c` | 明示的高耗電對照 | RFID **常開**、CPU 不睡 | 開始後 60 秒，之後強制關閉 |
| `d` / `h` / `x` | 讀結果／說明／取消預約 | 不啟動新測試 | — |

`c` 絕不是電池預設。若 b 失敗、a 正常，優先查睡眠／喚醒或 GPIO；若 a 也失敗而 c 正常，優先查 RFID 冷啟動、有效讀取時間、ON/OFF 接線與電源穩定。c 也失敗時，不應歸咎於睡眠。

## 實作界線

* UART1 只初始化一次，RX=44、TX=-1、9600 8N1、512-byte 接收 buffer。**先準備接收再拉 HIGH**；不等待 200ms 才開始聽、不反覆重設 UART、不丟棄新窗口的前幾個封包。這與 main 的啟動順序有差異，必須實測，不宣稱冷啟動已穩定。
* 新的遮擋啟動最長 10 秒掃描。取得已登錄晶片立刻結束並拉 LOW；沒讀到則逾時拉 LOW。尚未識別時，新的清空邊緣可再試一輪；持續遮擋、持續清空或無效量測不會自行無限重開。掃描中的動作不延長原窗口。
* 接收完整有效但未登錄的封包只計入 `other_valid`，本診斷版繼續等待已登錄 ID，不套用 main 的立即拒絕結案。它不產生正式身分或紀錄。
* Light-sleep 僅在 RFID 不需啟用且 UART 沒有待處理資料時嘗試，定時喚醒在下一個 100ms 測距 deadline 前。掃描期間不睡，不使用 UART 喚醒猜測首封包。感測器是同步 single-shot，量測完成才可能睡。
* 不使用 Deep-sleep 進行量測循環，不啟動攝影機、Wi-Fi、NTP、WebServer 或上傳工作。INT 腳仍不宣稱為閾值喚醒來源。
* 量測期間只寫 RAM，不輸出 Serial、不寫 NVS。完成後才保存一份約 6KB capture 到獨立 `battery-probe` namespace，不清除正式 `litter` namespace。等待中只有統計；觸發後保留 60 秒原始量測，另帶最後一筆前置樣本。
* 未保存前斷電會遺失本次 RAM 軌跡。`inflight` 標記用來指出中斷；上一份成功結果仍保留。`interrupted_run != 0` 時，不可把舊 capture 誤認為這次成功。NVS 不足或保存失敗不是通過，禁止為此直接清除正式資料。
* 完成後 RFID 仍 LOW；USB 未連接時停放於週期 Light-sleep。USB CDC 可能需要接回後 RESET 才可讀取，已保存 capture 不受 reset 影響。

## 讀取結果與判定

摘要：

* `commanded_on_ms`：GPIO HIGH 的累積時間，**不是量到的電流、線圈實際通電時間或電量**。
* `sleep_calls / sleep_ms / sleep_errors`：成功的 sleep 呼叫次數、實際返回前經過時間、失敗次數。i/b 空櫃應有大量成功睡眠、零錯誤，a/c 為零；不能只看有呼叫 API 就宣稱省電。
* `samples / invalid / max_gap_ms`：所有量測統計，包含等待觸發階段。原始樣本只保存觸發前最後一筆及其後 60 秒；i/c 從開始即保存。持續大於 200ms 的 gap 或大量 invalid 先判定量測品質未過。
* `scans / recognized / other_valid`：掃描窗口次數／成功窗口／其他有效 ID 封包，不是貓次數。
* `bytes / bad_frames / stale_bytes`：區分完全沒資料、校驗失敗、窗口外／舊資料。不能將有 bytes 誤判為成功讀取。
* `sample_drops / event_drops`：必須為零才算完整 trace；無效距離（flags bit0=0）不能當清空。
* `sensor_failed / aborted / interrupted_run`：非零或 `NO_VALID_CAPTURE` 都不能當通過。

`S,uptime_ms,distance_mm,flags` 是原始觀測；bit0=有效、bit1=遮擋、bit2=**該觀測觸發處理前** GPIO HIGH。相鄰 S 列可算真實觀測間隔。所有時間都是本次開機 uptime，沒有 NTP，不是台灣時刻；也沒有直接計算「如廁停留秒數」。

`E,uptime_ms,code,value,aux` 事件代碼：

| code | 意義 | value / aux |
|---|---|---|
| 1 / 2 | GPIO ON / OFF | GPIO 回讀 / 窗口編號 |
| 3 | 窗口開始 | 0 / 窗口編號 |
| 4 | 首個 UART byte | 0 / 距窗口起點 ms |
| 5 | 校驗有效晶片 | 已登錄貓 1/2 或未登錄 0 / 距窗口起點 ms |
| 6 | 無效封包 | — |
| 7 | 窗口結束 | 1=已識別、2=逾時、3=取消 / 窗口經過 ms |
| 8 | 感測器初始化失敗 | — |
| 9 | 丟棄舊窗口 backlog | bytes / — |
| 10 | 首次 sleep 失敗 | — / ESP 錯誤碼 |

先看兩次實際通過是否都有 `<200mm` 原始樣本，再看那段附近的 E3→E4→E5→E2。若兩次遮擋清楚但中間清空不足十秒，現有 main 的合併問題值得優先修；若離開完全沒有距離變化，先查測距幾何／gap，而非直接縮短固定超時。沒有 E4 查供電／ON-OFF／UART；有 E4 沒有有效 E5 查 frame、讀距、姿勢與暖機延遲。

### 最低硬體驗收（尚未執行）

- i 空櫃：`scans=0`、`commanded_on_ms=0`、GPIO1 實測低電位、`sleep_calls>0`、`sleep_errors=0`；測量電池端平均電流。與 c 同供電比較，必須觀察實際下降。GPIO LOW 不等於已證明模組進入低功耗。
- b 兩隻貓分別測試多次冷啟動／實際進出：距離樣本捕捉通過、得到對應註冊 ID、取得 ID 或逾時後 E2、待機不出現無緣無故重新啟用。建議每隻累積 10 次，不強迫動物配合。
- 已取得 ID 與未取得 ID 的掃描都須結束。實際 HIGH 窗口可能因一次阻塞量測略超過 10 秒，必須對照 `max_gap_ms`，不能解釋成無限常開。
- 自然動作未全部錄到時保留不確定性；不以調寬 200mm 閾值、猜離開時刻、或改回常開湊通過。
- 電流表**串聯**在電池端，不得使用電流檔跨接電池正負極；不要同時接升壓 5V 與電腦 USB。這版沒有 ADC 電流計，無法自行報出 mA 或剩餘電量。

## 軟體檢查

```powershell
python firmware/tests/battery_probe/run_host_tests.py
# 可指定已安裝的編譯器：
python firmware/tests/battery_probe/run_host_tests.py --cxx clang++
```

主機測試涵蓋 parser、校驗、跨窗口重置、RFID 開關政策、閾值、逾時、補掃、clock wrap、sleep deadline 與 100,000 次固定 seed 操作。另重播 **未修改的正式 Engine** 的已知短訪限制。它不是 ESP32 板端編譯，也不是實機／耗電測試。

Arduino CLI（須先有 ESP32 平台與 Pololu VL53L0X；或直接使用 IDE）：

```text
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3:CDCOnBoot=cdc firmware/tests/battery_probe
```

合併門檻：ESP32 實際編譯、以上待機／RFID 實測與 capture 審查仍待完成。後續正式修復還須整合按需供電、可靠校時、背景上傳互斥、事件品質／逾時標示及短訪規則，不能直接把這支離線 probe 當正式 logger。

## API 依據

- Espressif ESP32-S3 sleep：Light-sleep 保留執行狀態；無線須停用；定時喚醒／UART caveats：<https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html>
- Pololu VL53L0X single-shot / timing-budget API：<https://github.com/pololu/vl53l0x-arduino>

## 本次實際驗證紀錄

- 基線 `app_config.h` 與 `visit_logic.h` 的本機副本以 Git blob SHA 核對，與 GitHub 來源一致。
- Linux g++ 與 clang++ 主機測試：各自 PASS，260,445 assertions；包含上述已知缺陷重現，不代表 main 已修復。
- ESP32 平台編譯：**未執行**（此執行環境沒有 Arduino ESP32 toolchain）。
- 真實 RFID 啟停、睡眠喚醒、電流與電池續航：**未執行**（無實體裝置）。
