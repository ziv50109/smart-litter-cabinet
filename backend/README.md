
# Google Apps Script backend

1. Create an Apps Script project and paste in `Code.gs`.
2. Add `SPREADSHEET_ID` and a random `DEVICE_TOKEN` to Script Properties. Never store their real values in source code, Sheets, or GitHub.
3. Deploy as a Web App that executes as the owner. Device access is allowed, but every POST still requires token authentication.
4. Put the HTTPS `/exec` URL and token in the local `firmware/main/secrets.h`.

The receiver accepts only `device_token` and `session` at the top level, with the existing nine English protocol fields and no added fields. The sheet displays a fixed set of nine Chinese headers while Apps Script writes values in English-key order. On upgrade, the exact legacy English header row and the previous Chinese header row are migrated; invalid or extra columns remain rejected. It validates JSON types, integer fields, ranges, and the actual UTF-8 request size; prevents formula injection; rejects unexpected fields; deduplicates by `session_id`; and uses Script Lock for concurrent writes. `enter_time` and `exit_time` must both be empty (unknown offline event time) or both be UTC `YYYY-MM-DDTHH:mm:ssZ`; when present, ordering and `duration_sec` (within about two seconds) are checked. Historical timestamps have no lower cutoff, but timestamps more than ten minutes in the future are rejected. Error responses expose only safe error codes, never the token, spreadsheet ID, or stack trace.

UTC remains the API transport format. When data is written to Google Sheets, `enter_time` and `exit_time` are stored as actual spreadsheet date/time values instead of ISO text. The spreadsheet time zone is enforced as `Asia/Taipei`, and those columns are displayed as `yyyy/MM/dd HH:mm:ss`. Existing ISO timestamp strings in the two time columns are migrated once on the first request after deployment, so existing rows become sortable and usable with normal Sheets date/time functions as well.

The API field remains `duration_sec` and still carries integer seconds. In Google Sheets, the column header is displayed as `停留時間`; Apps Script stores it as a spreadsheet duration value and displays it as `[m]:ss` (for example, `241` seconds becomes `4:01`). Existing numeric second values are migrated at the same time as the timestamp migration. `sample_count` remains the number of valid VL53L0X distance samples collected during the session.

After changing `Code.gs`, create a new Web App deployment version (Deploy → Manage deployments → Edit → New version → Deploy). The existing `/exec` URL can remain unchanged.

Success returns `{"ok":true}`; a retried `session_id` returns `{"ok":true,"duplicate":true}`. Failures return `ok: false` with codes such as `unauthorized`, `invalid_*`, `inconsistent_time_duration`, `busy`, `server_not_configured`, or `storage_error`.

The ESP32 token is a bearer secret and can be recovered by someone with physical access. Rotate it in Script Properties and reflash the device if the unit is lost or its firmware is exposed.

---

# Google Apps Script 後端

1. 新增 Apps Script，貼上 `Code.gs`。
2. 在 Script Properties 建立 `SPREADSHEET_ID` 與隨機 `DEVICE_TOKEN`；不把實際值寫進程式、Sheet 或 GitHub。
3. 部署 Web App：Execute as owner，access 允許裝置呼叫，但每筆 POST 仍必須通過 token 驗證。
4. 將 `/exec` HTTPS URL 與 token 填入本機 `firmware/main/secrets.h`。

接收端只接受 `device_token` 與 `session` 兩個頂層欄位；`session` 必須包含原有的 9 個英文通訊欄位，沒有新增欄位。工作表顯示使用固定的 9 個中文表頭，Apps Script 依英文欄位順序寫入；部署升級時會遷移完全相符的舊英文表頭與前一版中文表頭，之後仍嚴格拒絕錯誤或多餘欄位。它會驗證 JSON 類型、整數欄位與範圍、實際 UTF-8 request 大小，拒絕公式注入與多餘欄位，用 `session_id` 去重，並用 Script Lock 防止並行寫入。`enter_time`/`exit_time` 必須同時為空（表示離線事件時間未知），或同時為 UTC `YYYY-MM-DDTHH:mm:ssZ`；有時間時也會驗證先後順序及 `duration_sec`（容許約 2 秒誤差）。歷史時間不設下限，但拒絕超過伺服器現在時間 10 分鐘的未來時間。錯誤回應只提供安全錯誤碼，不回傳 token、Spreadsheet ID 或 stack trace。

API 傳輸格式仍維持 UTC，不需要修改 ESP32。寫入 Google Sheets 時，`enter_time` / `exit_time` 會改存真正的日期時間值，而不是 ISO 文字；試算表時區會固定為 `Asia/Taipei`，兩欄顯示格式為 `yyyy/MM/dd HH:mm:ss`。部署新版後第一次收到 request 時，也會把這兩欄既有的 ISO 時間字串遷移成真正日期時間，因此舊資料也能正常排序、篩選及使用 Sheets 日期函式。

API 欄位仍維持 `duration_sec`，傳輸與驗證語意仍是整數秒數；Google Sheets 的欄名則顯示為「停留時間」，寫入時轉成真正的 duration 並以 `[m]:ss` 顯示，例如 `241` 秒會顯示成 `4:01`。既有的秒數資料會和日期時間一起在首次 request 時自動遷移。`sample_count` 則代表該次 session 期間取得的有效 VL53L0X 距離取樣數。

修改 `Code.gs` 後，需要到「部署 → 管理部署作業 → 編輯 → 建立新版本 → 部署」。原本的 `/exec` URL 可以維持不變。

正常回應為 `{"ok":true}`，重送已存在的 `session_id` 會回 `{"ok":true,"duplicate":true}`。失敗時 `ok` 為 `false`；常見錯誤碼包括 `unauthorized`、`invalid_*`、`inconsistent_time_duration`、`busy`、`server_not_configured` 及 `storage_error`。

注意：ESP32 裡的 token 屬 bearer secret，有實體存取的人可能取得；裝置遺失或程式曾公開時，在 Script Properties 更換 token 並重燒裝置。
