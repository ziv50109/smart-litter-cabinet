# XY-134.2K RFID reader test

## 中文

模組到貨後記錄接線與 UART 電平，先輸出 raw frame，再驗證 `$F` + 15 位數字 + 2 位 XOR checksum + `#` 的實際格式。要測試正常標籤、快速連續掃描、雜訊、斷線與未知標籤；公開記錄不放真實晶片號碼。

---

## English

When the module arrives, document its wiring and UART voltage level. First print raw frames, then verify the actual `$F` + 15 decimal digits + two-digit XOR checksum + `#` format. Test normal tags, rapid repeated scans, noise, disconnection, and unknown tags. Public test records must not contain real chip numbers.
