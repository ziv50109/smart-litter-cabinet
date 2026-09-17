# Google Apps Script backend

1. Create an Apps Script project and paste in `Code.gs`.
2. Add `SPREADSHEET_ID` and a random `DEVICE_TOKEN` to Script Properties. Never store their real values in source code, Sheets, or GitHub.
3. Deploy as a Web App that executes as the owner. Device access is allowed, but every POST still requires token authentication.
4. Put the HTTPS `/exec` URL and token in the local `firmware/main/secrets.h`.

The receiver accepts only `device_token` and `session` at the top level, with the existing nine English protocol fields and no added fields. It validates JSON types, ranges, request size, timestamps, duration consistency, unexpected fields, duplicates by `session_id`, and uses Script Lock for concurrent writes.

## Storage contract

The API contract remains UTC ISO timestamps plus integer seconds. Google Sheets stores the same semantics using Sheet-native numeric types so presentation can be controlled with normal Sheet formatting:

- `enter_time` / `exit_time`: API receives UTC ISO strings such as `2026-09-16T14:37:57Z`; the Sheet stores the same instant as a real date/time value.
- `duration_sec`: API receives integer seconds such as `81`; the Sheet stores the equivalent duration serial (`81 / 86400`).
- The display header is `停留時間`.
- The backend does **not** change the spreadsheet locale or time zone.
- The backend does **not** apply number/date formats. Configure display formatting in Google Sheets itself.

Recommended Sheet settings:

- Spreadsheet time zone: your desired display zone, e.g. `(GMT+08:00) Taipei`.
- Columns D:E (`進入時間`, `離開時間`): custom date/time format such as `yyyy/MM/dd HH:mm:ss`.
- Column F (`停留時間`): custom number format `[mm]:ss` so 81 seconds displays as `01:21`.

Changing the spreadsheet time zone changes how D:E are displayed because they are real timestamps. It does not change duration semantics in F.

For compatibility, an exact old English protocol header row or the previous Chinese header row using `停留秒數` is renamed to the current Chinese display headers. Existing historical data cells are not automatically converted; if an older deployment already wrote strings or raw seconds, correct those rows separately before applying one format to the entire column.

`sample_count` is the number of valid VL53L0X distance samples collected during the session.

## Manual Apps Script smoke test

Run `testWriteSample_()` from the Apps Script editor. It writes one clearly marked `TEST` row through the same `doPost()` path, using the configured `DEVICE_TOKEN` and a unique test `session_id`. With D:E formatted as `yyyy/MM/dd HH:mm:ss` and F formatted as `[mm]:ss`, the test row should show a normal local timestamp and `01:21` duration.

After changing `Code.gs`, create a new Web App deployment version (Deploy → Manage deployments → Edit → New version → Deploy). The existing `/exec` URL can remain unchanged.

Success returns `{"ok":true}`; a retried `session_id` returns `{"ok":true,"duplicate":true}`. Failures return `ok: false` with codes such as `unauthorized`, `invalid_*`, `inconsistent_time_duration`, `busy`, `server_not_configured`, or `storage_error`.

The ESP32 token is a bearer secret and can be recovered by someone with physical access. Rotate it in Script Properties and reflash the device if the unit is lost or its firmware is exposed.

---

# Google Apps Script 後端

1. 新增 Apps Script，貼上 `Code.gs`。
2. 在 Script Properties 建立 `SPREADSHEET_ID` 與隨機 `DEVICE_TOKEN`；不把實際值寫進程式、Sheet 或 GitHub。
3. 部署 Web App：Execute as owner，access 允許裝置呼叫，但每筆 POST 仍必須通過 token 驗證。
4. 將 `/exec` HTTPS URL 與 token 填入本機 `firmware/main/secrets.h`。

接收端只接受 `device_token` 與 `session` 兩個頂層欄位，維持既有 9 個英文通訊欄位。後端負責驗證 JSON、數值範圍、request 大小、時間合法性、停留秒數一致性、多餘欄位與 `session_id` 去重，並用 Script Lock 防止並行寫入。

## 儲存契約

API contract 仍維持 UTC ISO timestamp 與整數秒；Google Sheets 則使用 Sheet 原生可格式化的數值型別保存相同語意：

- `enter_time` / `exit_time`：API 接收 `2026-09-16T14:37:57Z` 這類 UTC ISO 字串；Sheet 寫入代表同一個時間點的真正日期時間值。
- `duration_sec`：API 仍接收整數秒，例如 `81`；Sheet 寫入等價的 duration serial，也就是 `81 / 86400`。
- Sheet 顯示欄名使用「停留時間」。
- 後端**不修改**試算表的地區或時區。
- 後端**不套用**日期時間或 duration 顯示格式；顯示方式由 Google Sheets 自己設定。

建議 Google Sheet 設定：

- 試算表時區：依你希望的顯示時區，例如 `(GMT+08:00) 台北`。
- D:E 欄（進入時間／離開時間）：自訂日期時間格式 `yyyy/MM/dd HH:mm:ss`。
- F 欄（停留時間）：自訂數字格式 `[mm]:ss`，因此 81 秒會顯示為 `01:21`。

更改試算表時區後，D:E 的顯示會跟著變，因為它們是真正的 timestamp；F 是 duration，不受時區影響。

為了相容既有 Sheet，若表頭完整符合舊英文 protocol 欄名，或只差第六欄仍為「停留秒數」的舊中文表頭，後端只會改成目前中文表頭。**既有歷史資料不會自動轉型**；如果舊版本已寫入 ISO 字串或整數秒，請先另外修正那些舊列，再對整欄套統一格式。

`sample_count` 代表該次 session 期間取得的有效 VL53L0X 距離取樣數。

## Apps Script 手動測試

在 Apps Script 編輯器直接執行 `testWriteSample_()`。它會使用 Script Properties 裡的 `DEVICE_TOKEN`，透過與 ESP32 相同的 `doPost()` 路徑寫入一筆清楚標示為 `TEST` 的測試資料，並產生唯一的 test `session_id`。當 D:E 設成 `yyyy/MM/dd HH:mm:ss`、F 設成 `[mm]:ss` 後，這筆測試資料應顯示正常的本地日期時間，停留時間應為 `01:21`。

修改 `Code.gs` 後，需要到「部署 → 管理部署作業 → 編輯 → 建立新版本 → 部署」。原本的 `/exec` URL 可以維持不變。

正常回應為 `{"ok":true}`，重送已存在的 `session_id` 會回 `{"ok":true,"duplicate":true}`。失敗時 `ok` 為 `false`；常見錯誤碼包括 `unauthorized`、`invalid_*`、`inconsistent_time_duration`、`busy`、`server_not_configured` 及 `storage_error`。

注意：ESP32 裡的 token 屬 bearer secret，有實體存取的人可能取得；裝置遺失或程式曾公開時，在 Script Properties 更換 token 並重燒裝置。

## Local regression tests / 本機回歸測試

From the repository root:

```sh
node --test backend/tests/code.test.cjs
```

The tests verify Sheet-native timestamp/duration storage, no backend time-zone/number-format mutation, header compatibility, duplicate idempotency, validation/security guards, and the manual smoke-test helper.
