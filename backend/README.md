# Google Apps Script backend

Receives ESP32 visit records and appends validated rows to the first sheet of a configured Google Spreadsheet.

## Setup

1. Create an Apps Script project and copy in `Code.gs`.
2. Add `SPREADSHEET_ID` and a random `DEVICE_TOKEN` to Script Properties.
3. Deploy as a Web App that executes as the owner and can be called by the device.
4. Put the HTTPS `/exec` URL and the same token in local `firmware/main/secrets.h`.

## Contract

The request has only `device_token` and `session`. A session contains:

`session_id`, `chip_id`, `cat_id`, `enter_time`, `exit_time`, `duration_sec`, `min_distance_mm`, `avg_distance_mm`, `sample_count`.

The backend validates field names, types and ranges, request size, timestamp/duration consistency, authentication, and duplicate `session_id` values. Sheet writes are protected by Script Lock.

`enter_time` and `exit_time` use UTC ISO timestamps. `duration_sec` is integer seconds. `sample_count` is the number of valid VL53L0X distance samples collected for the session.

Success returns `{"ok":true}`. Re-sending an existing `session_id` returns `{"ok":true,"duplicate":true}`. Failures return `ok: false` with an error code.

## Sheet representation

Headers:

`紀錄編號` · `晶片編號` · `貓咪` · `進入時間` · `離開時間` · `停留時間` · `最短距離（mm）` · `平均距離（mm）` · `取樣次數`

Timestamps are stored as native Sheet date/time values. Duration is stored as a Sheet duration serial (`seconds / 86400`). The backend does not change spreadsheet locale, time zone, or number formats.

Recommended display settings:

- Spreadsheet time zone: `(GMT+08:00) Taipei`
- D:E: `yyyy/MM/dd HH:mm:ss`
- F: `[mm]:ss`

## Test and deployment

Run `doTest()` in the Apps Script editor to write one `TEST` record through the same `doPost()` path used by the device.

From the repository root:

```sh
node --test backend/tests/code.test.cjs
```

After changing `Code.gs`, deploy a new Web App version. The existing `/exec` URL can remain unchanged.

`DEVICE_TOKEN` is a bearer secret. Rotate it and reflash the device if the device or firmware is exposed.

---

# Google Apps Script 後端

接收 ESP32 的使用紀錄，驗證後寫入指定 Google Spreadsheet 的第一個工作表。

## 設定

1. 建立 Apps Script 專案並貼入 `Code.gs`。
2. 在 Script Properties 建立 `SPREADSHEET_ID` 與隨機 `DEVICE_TOKEN`。
3. 部署為 Web App，由擁有者身分執行並允許裝置呼叫。
4. 將 HTTPS `/exec` URL 與相同 token 填入本機 `firmware/main/secrets.h`。

## API 資料格式

請求只包含 `device_token` 與 `session`。`session` 固定包含：

`session_id`、`chip_id`、`cat_id`、`enter_time`、`exit_time`、`duration_sec`、`min_distance_mm`、`avg_distance_mm`、`sample_count`。

後端會驗證欄位名稱、型別與範圍、請求大小、時間戳記與停留時間的一致性、`DEVICE_TOKEN`，以及重複的 `session_id`；寫入 Sheet 時使用 Script Lock。

`enter_time`、`exit_time` 使用 UTC ISO 時間戳記；`duration_sec` 是整數秒；`sample_count` 是該次事件取得的有效 VL53L0X 距離取樣數。

正常回應為 `{"ok":true}`；相同 `session_id` 重送時回 `{"ok":true,"duplicate":true}`。失敗時回 `ok: false` 與錯誤碼。

## Sheet 儲存

欄名：

`紀錄編號` · `晶片編號` · `貓咪` · `進入時間` · `離開時間` · `停留時間` · `最短距離（mm）` · `平均距離（mm）` · `取樣次數`

時間寫成 Google Sheets 原生日期時間值；停留時間寫成可格式化的時間長度數值（`秒數 / 86400`）。後端不修改試算表的地區、時區或顯示格式。

建議顯示設定：

- 試算表時區：`(GMT+08:00) 台北`
- D:E：`yyyy/MM/dd HH:mm:ss`
- F：`[mm]:ss`

## 測試與部署

在 Apps Script 編輯器執行 `doTest()`，會透過與裝置相同的 `doPost()` 路徑寫入一筆 `TEST` 紀錄。

在專案根目錄執行：

```sh
node --test backend/tests/code.test.cjs
```

修改 `Code.gs` 後重新部署 Web App 新版本即可，既有 `/exec` URL 可維持不變。

`DEVICE_TOKEN` 持有即具存取權限，應視為機密；裝置遺失或韌體外洩時應更換 token 並重新燒錄裝置。
