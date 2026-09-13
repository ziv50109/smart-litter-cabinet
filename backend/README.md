
# Google Apps Script backend

1. Create an Apps Script project and paste in `Code.gs`.
2. Add `SPREADSHEET_ID` and a random `DEVICE_TOKEN` to Script Properties. Never store their real values in source code, Sheets, or GitHub.
3. Deploy as a Web App that executes as the owner. Device access is allowed, but every POST still requires token authentication.
4. Put the HTTPS `/exec` URL and token in the local `firmware/main/secrets.h`.

The receiver accepts only `device_token` and `session` at the top level, with the existing nine English protocol fields and no added fields. The sheet displays a fixed set of nine Chinese headers while Apps Script writes values in English-key order. On upgrade, only an exact legacy English header row is migrated once; invalid or extra columns remain rejected. It validates JSON types, integer fields, ranges, and the actual UTF-8 request size; prevents formula injection; rejects unexpected fields; deduplicates by `session_id`; and uses Script Lock for concurrent writes. `enter_time` and `exit_time` must both be empty (unknown offline event time) or both be UTC `YYYY-MM-DDTHH:mm:ssZ`; when present, ordering and `duration_sec` (within about two seconds) are checked. Historical timestamps have no lower cutoff, but timestamps more than ten minutes in the future are rejected. Error responses expose only safe error codes, never the token, spreadsheet ID, or stack trace.

Success returns `{"ok":true}`; a retried `session_id` returns `{"ok":true,"duplicate":true}`. Failures return `ok: false` with codes such as `unauthorized`, `invalid_*`, `inconsistent_time_duration`, `busy`, `server_not_configured`, or `storage_error`.

The ESP32 token is a bearer secret and can be recovered by someone with physical access. Rotate it in Script Properties and reflash the device if the unit is lost or its firmware is exposed.

---

# Google Apps Script 後端

1. 新增 Apps Script，貼上 `Code.gs`。
2. 在 Script Properties 建立 `SPREADSHEET_ID` 與隨機 `DEVICE_TOKEN`；不把實際值寫進程式、Sheet 或 GitHub。
3. 部署 Web App：Execute as owner，access 允許裝置呼叫，但每筆 POST 仍必須通過 token 驗證。
4. 將 `/exec` HTTPS URL 與 token 填入本機 `firmware/main/secrets.h`。

接收端只接受 `device_token` 與 `session` 兩個頂層欄位；`session` 必須包含原有的 9 個英文通訊欄位，沒有新增欄位。工作表顯示使用固定的 9 個中文表頭，Apps Script 依英文欄位順序寫入；部署升級時只會將完全相符的舊英文表頭遷移一次，之後仍嚴格拒絕錯誤或多餘欄位。它會驗證 JSON 類型、整數欄位與範圍、實際 UTF-8 request 大小，拒絕公式注入與多餘欄位，用 `session_id` 去重，並用 Script Lock 防止並行寫入。`enter_time`/`exit_time` 必須同時為空（表示離線事件時間未知），或同時為 UTC `YYYY-MM-DDTHH:mm:ssZ`；有時間時也會驗證先後順序及 `duration_sec`（容許約 2 秒誤差）。歷史時間不設下限，但拒絕超過伺服器現在時間 10 分鐘的未來時間。錯誤回應只提供安全錯誤碼，不回傳 token、Spreadsheet ID 或 stack trace。

正常回應為 `{"ok":true}`，重送已存在的 `session_id` 會回 `{"ok":true,"duplicate":true}`。失敗時 `ok` 為 `false`；常見錯誤碼包括 `unauthorized`、`invalid_*`、`inconsistent_time_duration`、`busy`、`server_not_configured` 及 `storage_error`。

注意：ESP32 裡的 token 屬 bearer secret，有實體存取的人可能取得；裝置遺失或程式曾公開時，在 Script Properties 更換 token 並重燒裝置。
