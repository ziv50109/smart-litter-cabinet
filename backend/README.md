
# Google Apps Script backend

1. Create an Apps Script project and paste in `Code.gs`.
2. Add `SPREADSHEET_ID` and a random `DEVICE_TOKEN` to Script Properties. Never store their real values in source code, Sheets, or GitHub.
3. Deploy as a Web App that executes as the owner. Device access is allowed, but every POST still requires token authentication.
4. Put the HTTPS `/exec` URL and token in the local `firmware/main/secrets.h`.

The receiver accepts only `device_token` and `session` at the top level, with the existing nine English protocol fields and no added fields. The sheet displays a fixed set of nine Chinese headers while Apps Script writes values in English-key order. On upgrade, the exact legacy English header row and the previous Chinese header row are migrated; invalid or extra columns remain rejected. It validates JSON types, integer fields, ranges, and the actual UTF-8 request size; prevents formula injection; rejects unexpected fields; deduplicates by `session_id`; and uses Script Lock for concurrent writes. `enter_time` and `exit_time` must both be empty (unknown offline event time) or both be UTC `YYYY-MM-DDTHH:mm:ssZ`; when present, ordering and `duration_sec` (within about two seconds) are checked. Historical timestamps have no lower cutoff, but timestamps more than ten minutes in the future are rejected. Error responses expose only safe error codes, never the token, spreadsheet ID, or stack trace.

UTC remains the API transport format. When data is written to Google Sheets, `enter_time` and `exit_time` are stored as actual spreadsheet date/time values instead of ISO text. The spreadsheet time zone is enforced as `Asia/Taipei`, and those columns are displayed as `yyyy/MM/dd HH:mm:ss`. Existing ISO timestamp strings in the two time columns are migrated once on the first request after deployment, so existing rows become sortable and usable with normal Sheets date/time functions as well.

The API field remains `duration_sec` and still carries integer seconds. In Google Sheets, the column header is displayed as `停留時間`; Apps Script stores it as a spreadsheet duration value and displays it as `[mm]:ss` (for example, `241` seconds becomes `04:01`). Existing numeric second values are migrated at the same time as the timestamp migration. `sample_count` remains the number of valid VL53L0X distance samples collected during the session.

Legacy numeric seconds are migrated as self-contained formulas such as `=241/86400`. Their result is still a numeric duration; retaining the original seconds in the same cell makes retries safe, including `86400` seconds (`1440:00`). New API rows store numeric day fractions directly. Migration markers include the version, spreadsheet ID, and sheet ID. Legacy headers are renamed only after the migrated values and formats have been flushed. Duplicate requests repair the stored row's date/duration formats before reporting success.

Back up the sheet before upgrading, and do not manually rename legacy headers or edit/reorder rows during migration. Completed earlier duration revisions using `[m]:ss` are accepted without another division. If an earlier experimental revision already renamed the header but left unformatted numeric values, the unit may be ambiguous (for example, `1` second versus `1` day). The receiver returns `storage_error` rather than guessing. Restore the affected duration column and its old header from the pre-upgrade backup before retrying; this cannot reconstruct values already corrupted by an older revision. Legacy duration formulas other than the explicit seconds/day conversion also require manual unit verification.

After copying the updated `backend/Code.gs` into your Apps Script project, create a new Web App deployment version (Deploy → Manage deployments → Edit → New version → Deploy). The existing `/exec` URL can remain unchanged.

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

API 欄位仍維持 `duration_sec`，傳輸與驗證語意仍是整數秒數；Google Sheets 的欄名則顯示為「停留時間」，寫入時轉成真正的 duration 並以 `[mm]:ss` 顯示，例如 `241` 秒會顯示成 `04:01`。既有的秒數資料會和日期時間一起在首次 request 時自動遷移。`sample_count` 則代表該次 session 期間取得的有效 VL53L0X 距離取樣數。

舊秒數會遷移成 `=241/86400` 這種自帶原始秒數的公式；計算結果仍是可排序與計算的數值型 duration。原值與單位換算同時存在同一格，因此中斷重試不會再次除以 86400；一天會維持 `1440:00`。新 API 紀錄直接存數值型天數。遷移標記包含版本、Spreadsheet ID 與工作表 ID，舊表頭只會在資料與格式 flush 成功後更名。重送相同紀錄時會先補齊該列的日期／duration 格式，再回覆成功。

升級前先備份工作表；不要自行更名舊表頭，也不要在遷移期間手動編輯或重新排列資料列。前期版本已完成換算、格式為 `[m]:ss` 的資料會直接沿用，不重複換算。如果曾部署中途版本，表頭已更名但留下未格式化的數值，可能無法辨識單位（例如 `1` 是一秒或一天）；此時回傳 `storage_error`，不猜測或重複除法。請由升級前備份還原受影響的停留欄位及舊表頭後重試；舊版已改壞的數值無法憑空復原。舊秒數欄若有人工作成其他公式，也需先人工核對單位。

把更新的 `backend/Code.gs` 貼回 Apps Script 專案後，需要到「部署 → 管理部署作業 → 編輯 → 建立新版本 → 部署」。原本的 `/exec` URL 可以維持不變。

正常回應為 `{"ok":true}`，重送已存在的 `session_id` 會回 `{"ok":true,"duplicate":true}`。失敗時 `ok` 為 `false`；常見錯誤碼包括 `unauthorized`、`invalid_*`、`inconsistent_time_duration`、`busy`、`server_not_configured` 及 `storage_error`。

注意：ESP32 裡的 token 屬 bearer secret，有實體存取的人可能取得；裝置遺失或程式曾公開時，在 Script Properties 更換 token 並重燒裝置。

## Local regression tests / 本機回歸測試

From the repository root (Node.js 22 tested; no package installation required):

```sh
node --test backend/tests/code.test.cjs
```

These tests execute `Code.gs` in a Node VM with in-memory Apps Script service mocks, including before/after-write failures, partial migration writes, per-sheet markers, duplicate repair, unit boundaries, and validation guards. They do **not** execute Apps Script, emulate Sheets rendering, or deploy the Web App.

本機測試只驗證控制流程與故障復原，不等於 Google Sheets 整合驗收。部署後請用測試紀錄確認：UTC `2026-09-16T14:37:57Z` 顯示 `2026/09/16 22:37:57`；0、241、3665 秒分別顯示 `00:00`、`04:01`、`61:05`；`ISNUMBER` 判定日期與停留欄為數值，重送同一 `session_id` 不增加紀錄。

Format reference: https://developers.google.com/workspace/sheets/api/guides/formats#date_and_time_format_tokens
