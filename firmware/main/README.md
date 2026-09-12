# 貓砂櫃主韌體

以 VL53L0X 觀察入口活動，以 XY-134.2K 讀取 ISO11784/85 晶片，將完成事件送往 Google Sheets。接線、供電與讀距見 [硬體說明](../../hardware/README.md)。

## 檔案與使用

`main.ino` 負責硬體、保存與背景傳輸；`visit_logic.h` 是正式事件狀態機；`app_config.h` 定義腳位及時間；`certificates.h` 為公開憑證；`secrets.example.h` 是空白設定介面。部署值只放在被 Git 排除的本機 `secrets.h`。

Arduino IDE 安裝 ESP32 平台與 Pololu VL53L0X，選 XIAO ESP32-S3，Verify 後 Upload；Serial 為 115200 baud。GPIO 不變：XY ON/OFF=1、ToF INT/SDA/SCL=2/5/6、ESP32 TX→XY RXD=43、ESP32 RX←XY TXD=44。UART 為 9600 8N1，封包為 $F＋15 位數字＋XOR＋#。

## 事件規則

- 待機與活動皆約每 100ms 量測。有效距離 <200mm 只啟動 RFID 候選掃描，不另加遮擋防抖；只有讀到本機設定中已登錄的兩隻貓，才使用首次遮擋時間建立正式事件。有效 >=200mm 為清空。
- 入口活動中的反覆遮擋合併為同一事件。累積 10 秒已驗證的有效清空樣本才轉為「推定在內部」，不結案。每筆有效樣本最多貢獻一個 100ms 排程週期；未取樣空窗不計時，短暫排程延遲不清除先前進度，無效讀值或再次遮擋才重新累積。
- 完全沒有可判定封包時，RFID 候選掃描最多 10 秒。讀到已登錄晶片立即接受；讀到完整但未登錄的晶片立即視為干擾並停止該輪，不繼續等待。入口未取得已登錄身分就不建立事件、不保存、不上傳，並等待入口恢復後才接受新觸發。
- 推定在內部之後，下一次遮擋建立離開候選並再次掃 RFID。只有離開後連續清空 10 秒、且該輪掃描已完成，才正常結案。離開階段反覆活動不重啟掃描。
- 從事件起點滿 90 秒仍無離開候選，以 `no_exit_timeout` 結案。90 秒前已有候選，可收尾至第 100 秒；屆時尚未符合正常條件，使用 `exit_unconfirmed_timeout`。
- 正式事件已知 A 時，離開漏讀仍保留 A；離開讀到已登錄的 B 則鎖定衝突、保留 A 供診斷，但整筆捨棄不上傳。離開讀到未登錄晶片會立即停止該輪並視為干擾／漏讀，不更改 A、不製造衝突。系統不建立 unknown 如廁紀錄。
- 正常結案後立即接受新遮擋，不另設冷卻。強制結案時入口仍遮擋、無效或資訊過期，必須先看到有效清空，再接受新遮擋。

事件起點不因掃描、清空或階段改變而重設。有離開候選時，停留時間與推定離開時間採首次候選時間；無候選則採起點加 90 秒。不計入收尾及網路等待。起點沒有可靠 UTC 時，日期欄留空，不以上傳時間回填。

## 保存與傳輸

完成事件先複製成獨立快照。Wi-Fi、NTP 與 HTTPS 在背景工作執行，不共用下一筆的可變事件資料。待傳 NVS 最多 8 筆，依序補送；HTTP 或佇列確認失敗至少隔 60 秒重試，後端以 session_id 去重。佇列滿或保存失敗不覆寫既有紀錄，顯示失敗計數且繼續感測。

活動事件於開始、取得晶片及每 30 秒備份。重啟後舊活動備份只保留為本機中斷紀錄，不自動上傳、不重新給 90 秒；入口恢復後才接受新事件。中斷槽只有一筆，已滿或保存／刪除失敗時保留原資料並停用活動備份，顯示錯誤；已完成的待傳佇列仍獨立處理。錯誤計數為本次開機的 RAM 診斷值，不是永久稽核紀錄。

POST 與 ContentService 重新導向分別建立 HTTPS 連線，使用 Google R1/R4 公開憑證鏈。後端九個欄位與通訊格式不變。

## 除錯與限制

本機設定 `DEBUG_WEB_SERVER true` 可啟用 LAN 頁面。Web 與 Serial 分開顯示階段、事件／清空時間、本輪 RFID 結果、保留身分、衝突、最近結案原因及保存／上傳狀態。晶片編號遮罩；不要將除錯頁暴露到網際網路。除錯模式維持 Wi-Fi 連線。

單一入口感測器不能證明移動方向或已如廁。探頭後退出、或在清空確認期間完整往返，可能合併或以 timeout 結案。結案原因只在本機呈現，Google Sheets 的既有欄位無法區分正常與 timeout。約 100ms 的排程不是硬即時保證；真實量測間隔、天線位置、兩隻貓的動作、長時間觀察後補讀及耗電仍須實機驗證。

---

# Main litter-cabinet firmware

VL53L0X observes entrance activity; XY-134.2K identifies ISO11784/85 chips; completed events are sent to Google Sheets. See [hardware](../../hardware/README.md) for wiring, power and measured range.

## Files and use

`main.ino` owns hardware, persistence and background transport; `visit_logic.h` is the production event engine; `app_config.h` owns pins and timing; `certificates.h` holds public certificates; `secrets.example.h` defines the empty configuration interface. Deployment values belong only in ignored local `secrets.h`.

Install the ESP32 platform and Pololu VL53L0X in Arduino IDE; select XIAO ESP32-S3, Verify, then Upload. Serial uses 115200 baud. Pins are unchanged: XY enable=1, ToF INT/SDA/SCL=2/5/6, ESP32 TX→XY RXD=43, ESP32 RX←XY TXD=44. UART is 9600 8N1 with $F + 15 decimal digits + XOR + # frames.

## Event rules

- Idle and active measurements are scheduled about every 100ms. A valid <200mm reading starts only an RFID candidate scan without extra blockage debounce. A formal event, anchored to that first blockage, is created only after one of the two locally registered cats is read. Valid >=200mm is clear.
- Repeated entrance activity belongs to one event. Ten seconds of verified valid-clear samples changes the phase to presumed inside without closing it. Each sample contributes at most one 100ms schedule period: unsampled gaps add no time, short scheduling delays preserve prior progress, and invalid or blocked readings reset confirmation.
- An entry candidate waits up to ten seconds only when no decisive frame arrives. A registered chip is accepted immediately; a complete unregistered chip is rejected as interference immediately and ends that scan. Without a registered entry identity, no event is created, stored or uploaded; another attempt requires entrance recovery first.
- The next blockage after presumed inside starts an exit candidate and another RFID scan. Normal closure requires ten seconds of continuous clear after exit and a completed scan. Exit movement does not restart scanning.
- No exit candidate by 90 seconds from entry closes with `no_exit_timeout`. A candidate before 90 seconds may finish through second 100; if normal conditions are still unmet, close with `exit_unconfirmed_timeout`.
- Once a formal A event exists, a missed exit read preserves A. Reading the other registered cat B creates a sticky conflict: retain A for diagnosis but discard the event. An unregistered exit chip immediately ends that scan as interference/a miss without changing A or creating a conflict. The system does not create unknown visit records.
- Normal closure allows the next blockage immediately, without a cooldown. Forced closure with a blocked, invalid or stale entrance observation requires valid clear before another blockage can trigger.

The event start never resets during scans or phase changes. Duration and inferred exit time use the first exit candidate, or entry plus 90 seconds if none exists; finishing and network delays are excluded. Without reliable UTC at entry, date fields remain blank and are never filled with upload time.

## Persistence and delivery

Completed events become independent snapshots. Wi-Fi, NTP and HTTPS run in a background task without sharing the next mutable event. The NVS pending queue holds up to eight events and drains in order. HTTP or queue-acknowledgement failures retry no sooner than 60 seconds later; the backend deduplicates session_id. Full queues or failed saves preserve existing events, increment visible failure counters and do not stop sensing.

Active events are checkpointed at entry, identity reception and every 30 seconds. After reboot, old active journals are kept locally as interrupted records, never automatically uploaded or given a fresh 90 seconds. New events wait for entrance recovery. There is one interrupted slot; a full slot or failed save/removal preserves existing data, disables active journaling and reports an error. Completed pending records remain independent. Diagnostic error counters live in RAM for this boot, not a permanent audit log.

POST and ContentService redirects use separate HTTPS connections and public Google R1/R4 trust roots. The nine backend fields and transport format are unchanged.

## Debugging and limits

Enable `DEBUG_WEB_SERVER true` locally for the LAN page. Web and Serial distinguish phase, event/clear elapsed time, per-scan result, retained identity, conflict, latest closure reason and storage/upload status. Chip IDs are masked. Keep the page off the public internet; debug mode keeps Wi-Fi connected.

A single entrance sensor cannot establish direction or prove toileting. Peeking and retreating, or a complete round trip within clear confirmation, can merge into one event or end by timeout. Closure reasons are local only; existing Sheets fields cannot distinguish normal closure from timeout. The approximately 100ms schedule is not a hard real-time guarantee. Actual sample gaps, antenna placement, both cats' movements, delayed RFID rescans and power consumption require hardware validation.
