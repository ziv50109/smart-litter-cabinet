# Hardware validation tests

## 中文

每個資料夾只驗證一項硬體，並保留「程式版本、接線、實測結果、通過條件」，避免主韌體問題時無法分辨是感測器還是系統邏輯。

1. `vl53l0x-distance-web`：距離、timeout、門口解析度與 1 秒輪詢。
2. `xy134-rfid-reader`：UART frame、checksum、讀取距離、重複掃描與兩個標籤。
3. `power-measurement`：idle、感測中、Wi-Fi 上傳峰值與每日事件數，再估算 1000 mAh 續航。

---

## English

Each directory validates one hardware component and records the sketch version, wiring, measured results, and acceptance criteria. This makes it easier to separate sensor failures from main-firmware logic issues.

1. `vl53l0x-distance-web`: distance, timeout behavior, doorway resolution, and one-second polling.
2. `xy134-rfid-reader`: UART frame, checksum, read range, repeated scans, and both tags.
3. `power-measurement`: idle, sensing, Wi-Fi upload peaks, and daily event count for estimating 1000 mAh runtime.
