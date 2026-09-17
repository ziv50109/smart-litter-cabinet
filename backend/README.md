# Google Apps Script backend

1. Create an Apps Script project and paste in `Code.gs`.
2. Add `SPREADSHEET_ID` and a random `DEVICE_TOKEN` to Script Properties. Never store their real values in source code, Sheets, or GitHub.
3. Deploy as a Web App that executes as the owner. Device access is allowed, but every POST still requires token authentication.
4. Put the HTTPS `/exec` URL and token in the local `firmware/main/secrets.h`.

The receiver accepts only `device_token` and `session` at the top level, with the existing nine English protocol fields and no added fields. It validates JSON types, ranges, request size, timestamps, duration consistency, unexpected fields, duplicates by `session_id`, and uses Script Lock for concurrent writes.

## Storage contract

The backend and data sheet preserve source semantics instead of applying presentation formatting:

- `enter_time` / `exit_time` stay UTC ISO strings exactly as accepted from the device, for example `2026-09-16T14:37:57Z`.
- `duration_sec` stays integer seconds, for example `241`.
- The display header is `停留時間`, but the stored value is still seconds.
- The backend does not change the spreadsheet locale/time zone and does not apply date/time or duration number formats.
- Presentation such as Taiwan local time or `mm:ss` belongs in a separate Sheet view/formula or application UI.

For compatibility, an exact old English protocol header row or the previous Chinese header row using `停留秒數` is renamed to the current Chinese display headers. Existing data cells are not converted.

`sample_count` is the number of valid VL53L0X distance samples collected during the session.

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

後端與資料表只保存 source 語意，不處理顯示格式：

- `enter_time` / `exit_time` 保持裝置送來且驗證通過的 UTC ISO 字串，例如 `2026-09-16T14:37:57Z`。
- `duration_sec` 保持整數秒，例如 `241`。
- Sheet 顯示欄名使用較中性的「停留時間」，但儲存值仍然是秒數。
- 後端不修改試算表的地區／時區，也不套用日期時間或 duration 格式。
- 台灣時間、`mm:ss` 等呈現方式應由另一個 Sheet View／公式或真正的前端 UI 處理。

為了相容既有 Sheet，若表頭完整符合舊英文 protocol 欄名，或只差第六欄仍為「停留秒數」的舊中文表頭，後端只會把表頭改成目前中文顯示名稱；**既有資料值不做任何換算**。

`sample_count` 代表該次 session 期間取得的有效 VL53L0X 距離取樣數。

修改 `Code.gs` 後，需要到「部署 → 管理部署作業 → 編輯 → 建立新版本 → 部署」。原本的 `/exec` URL 可以維持不變。

正常回應為 `{"ok":true}`，重送已存在的 `session_id` 會回 `{"ok":true,"duplicate":true}`。失敗時 `ok` 為 `false`；常見錯誤碼包括 `unauthorized`、`invalid_*`、`inconsistent_time_duration`、`busy`、`server_not_configured` 及 `storage_error`。

注意：ESP32 裡的 token 屬 bearer secret，有實體存取的人可能取得；裝置遺失或程式曾公開時，在 Script Properties 更換 token 並重燒裝置。

## Local regression tests / 本機回歸測試

From the repository root:

```sh
node --test backend/tests/code.test.cjs
```

The tests verify that accepted UTC strings and integer seconds are stored without presentation conversion, legacy headers are renamed without touching existing data, duplicates remain idempotent, and validation/security guards still work.
