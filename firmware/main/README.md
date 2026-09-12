# 貓砂櫃主韌體

以 VL53L0X 觀察入口活動，以 XY-134.2K 讀取 ISO11784/85 晶片，將完成事件送往 Google Sheets。接線、供電與讀距見 [硬體說明](../../hardware/README.md)。

## 檔案與使用

`main.ino` 負責硬體、保存與背景傳輸；`visit_logic.h` 是正式事件狀態機；`app_config.h` 定義腳位及時間；`certificates.h` 為公開憑證；`secrets.example.h` 是空白設定介面。部署值只放在被 Git 排除的本機 `secrets.h`。

Arduino IDE 安裝 ESP32 平台與 Pololu VL53L0X，選 XIAO ESP32-S3，Verify 後 Upload；Serial 為 115200 baud。GPIO 不變：XY ON/OFF=1、ToF INT/SDA/SCL=2/5/6、ESP32 TX→XY RXD=43、ESP32 RX←XY TXD=44。UART 為 9600 8N1，封包為 $F＋15 位數字＋XOR＋#。

## 事件規則

- 待機與活動皆約每 100ms 量測。有效距離 <200mm 只啟動 RFID 候選掃描，不另加遮擋防抖；只有讀到本機設定中已登錄的兩隻貓，才使用首次遮擋時間建立正式事件。有效 >=200mm 為清空。
- 入口活動中的反覆遮擋合併為同一事件。有效距離持續 >=200mm 滿 10 秒才轉為「推定在內部」，不結案。相鄰有效清空樣本相隔不超過 1 秒時，依實際經過時間累積；超過 1 秒視為取樣中斷，當前樣本重新開始計時。距離無效或再次遮擋也歸零。
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

本機設定 `DEBUG_WEB_SERVER true` 可啟用 LAN 事件 log 頁面。上方保留即時距離、UART 計數及狀態；逐列記錄入口／離開候選掃描開始、完整晶片 ID、貓咪、拒絕／校驗失敗、掃描結束、進出判定及事件結案。上傳使用事件編號與嘗試次數關聯連線、校時、API 開始、POST、重新導向、API 結束及重試，並顯示 HTTP 狀態和耗時。API 開始包含 HTTPS 連線準備，只有後端成功回應才顯示成功。

ESP32 RAM 保留最近 128 筆，重新整理可讀回，重啟清空；覆寫數可見。本頁最多保留 2000 筆，提供暫停自動捲動及下載目前紀錄 JSON；超出或漏接會提示。時間已校準時顯示台灣時間，否則只顯示開機後毫秒數。LAN log 的晶片 ID 完整顯示，Serial 及既有狀態 API 維持遮罩；Wi-Fi 密碼、API token 與原始 HTTP 回應不進 log。不要將頁面公開到網際網路。除錯模式維持 Wi-Fi 連線。

`debug_log.h` 提供固定容量、跨執行緒保護的 RAM 紀錄；`/api/logs?after=序號` 每頁最多回傳 16 筆。後端 Sheets 格式與進出計時不受 log 功能影響。

UART 在掃描窗口內外都持續處理，每次最多接收 96 bytes，並在距離／網頁服務前先接收。RFID 啟動流程對齊獨立測試版：ON/OFF 設 HIGH，等待 200ms，再以 9600 8N1、RX=GPIO44、TX=-1 初始化 UART1。模組持續啟用，候選掃描結束、逾時及事件結案都不關閉；GPIO43 接線可保留，但 RFID UART 不配置傳送腳位。常開比按需啟用耗電較高；「掃描窗口」僅控制事件是否採用晶片，不控制模組電源。開始掃描不清空 UART：既有緩衝資料及跨窗口封包只供診斷，不建立事件、不借給下一輪；在軟體處理時已到掃描期限的封包也不採用，因為沒有硬體接收時間戳可證明其到達時間。

LAN log 在解析前以最多 16 bytes 一列保留 HEX 與可讀文字，標示窗口內或窗口外／舊資料；有效窗口外封包顯示 ID 與貓名，但不影響身分判定。掃描結束列出接收量、無效計數及尚未完成的封包長度，超長與被新起始符取代的半包另列原因。原始 UART 僅在本機除錯日誌公開，不送往 Sheets；雜訊或大量資料仍可能使 128 筆 RAM 日誌覆寫，應及時下載。

即時狀態顯示毫秒精度的清空進度、完成狀態或歸零原因。已有累積進度因距離無效、再次遮擋或取樣中斷超過 1 秒而歸零時，事件 log 會另列「清空計時重算」。

單一入口感測器不能證明移動方向或已如廁。探頭後退出、或在清空確認期間完整往返，可能合併或以 timeout 結案。結案原因只在本機呈現，Google Sheets 的既有欄位無法區分正常與 timeout。約 100ms 的排程不是硬即時保證；真實量測間隔、天線位置、兩隻貓的動作、長時間觀察後補讀及耗電仍須實機驗證。

---

# Main litter-cabinet firmware

VL53L0X observes entrance activity; XY-134.2K identifies ISO11784/85 chips; completed events are sent to Google Sheets. See [hardware](../../hardware/README.md) for wiring, power and measured range.

## Files and use

`main.ino` owns hardware, persistence and background transport; `visit_logic.h` is the production event engine; `app_config.h` owns pins and timing; `certificates.h` holds public certificates; `secrets.example.h` defines the empty configuration interface. Deployment values belong only in ignored local `secrets.h`.

Install the ESP32 platform and Pololu VL53L0X in Arduino IDE; select XIAO ESP32-S3, Verify, then Upload. Serial uses 115200 baud. Pins are unchanged: XY enable=1, ToF INT/SDA/SCL=2/5/6, ESP32 TX→XY RXD=43, ESP32 RX←XY TXD=44. UART is 9600 8N1 with $F + 15 decimal digits + XOR + # frames.

## Event rules

- Idle and active measurements are scheduled about every 100ms. A valid <200mm reading starts only an RFID candidate scan without extra blockage debounce. A formal event, anchored to that first blockage, is created only after one of the two locally registered cats is read. Valid >=200mm is clear.
- Repeated entrance activity belongs to one event. A valid distance >=200mm sustained for ten seconds changes the phase to presumed inside without closing it. Consecutive valid-clear samples no more than one second apart accumulate actual elapsed time. A gap over one second starts a new run at the current sample; invalid or blocked readings also reset it.
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

Enable `DEBUG_WEB_SERVER true` locally for the LAN event log. Live distance, UART counters and state remain above chronological rows for entry/exit candidate scan starts, full chip IDs, cat names, rejection/checksum failures, scan ends, state decisions and closure. Upload rows correlate connection, clock sync, API start, POST, redirects, API end and retries by event ID and attempt number, including HTTP status and duration. API start includes HTTPS connection preparation; success requires a successful backend response.

ESP32 RAM retains the latest 128 rows across page refreshes, but not device restarts; overwrite counts are visible. The browser retains up to 2000 rows, supports pausing automatic scrolling and downloading its current rows as JSON, and reports missed or discarded rows. Synchronized timestamps display Taiwan time; otherwise only uptime milliseconds are shown. Full chip IDs appear in the LAN log; Serial and the existing status API remain masked. Wi-Fi passwords, API tokens and raw HTTP responses are never logged. Keep the page off the public internet; debug mode keeps Wi-Fi connected.

`debug_log.h` provides the fixed-capacity, thread-protected RAM log; `/api/logs?after=sequence` returns up to 16 rows per page. Logging leaves the Sheets schema and visit timing unchanged.

UART is serviced inside and outside scan windows, up to 96 bytes per pass, including before distance/web service. Startup matches the standalone reader: set ON/OFF HIGH, wait 200ms, then initialize UART1 at 9600 8N1 with RX=GPIO44 and TX=-1. The reader stays enabled after scan completion, timeout and event closure. GPIO43 wiring may remain, but the RFID UART does not assign a transmit pin. Continuous operation consumes more power than on-demand activation; scan windows control identity acceptance, not reader power. Starting a scan does not drain UART: existing backlog and cross-window frames are diagnostic only and cannot create events or carry identity into the next scan. Frames processed at or after the deadline are also excluded because hardware arrival timestamps are unavailable.

Before parsing, the LAN log records HEX and printable text in rows of up to 16 bytes, labeled in-window or outside/stale. Valid outside-window frames show their ID and cat without affecting event identity. Scan summaries retain byte and invalid-frame counts plus the unfinished frame length; oversized frames and partial frames replaced by a new start marker have explicit diagnostics. Raw UART stays in local debug logs, never Sheets. Noise or heavy traffic can still overwrite the 128-row RAM ring; download promptly.

Live status shows millisecond-precision clear progress, completion, or the current reset reason. If accumulated progress is reset by an invalid reading, another blockage, or a sample gap over one second, the event log adds a `clear timer restarted` diagnostic row.

A single entrance sensor cannot establish direction or prove toileting. Peeking and retreating, or a complete round trip within clear confirmation, can merge into one event or end by timeout. Closure reasons are local only; existing Sheets fields cannot distinguish normal closure from timeout. The approximately 100ms schedule is not a hard real-time guarantee. Actual sample gaps, antenna placement, both cats' movements, delayed RFID rescans and power consumption require hardware validation.
