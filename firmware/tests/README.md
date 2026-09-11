# 硬體驗證測試

每個資料夾只驗證一項硬體，並保留「程式版本、接線、實測結果、通過條件」，避免主韌體問題時無法分辨是感測器還是系統邏輯。

1. `vl53l0x_distance_web`：距離、timeout、門口解析度與 1 秒輪詢。
2. `xy134-rfid-reader`：UART frame、checksum、讀取距離、重複掃描與兩個標籤。

---

# Hardware validation tests

Each directory validates one hardware component and records the sketch version, wiring, measured results, and acceptance criteria. This makes it easier to separate sensor failures from main-firmware logic issues.

1. `vl53l0x_distance_web`: distance, timeout behavior, doorway resolution, and one-second polling.
2. `xy134-rfid-reader`: UART frame, checksum, read range, repeated scans, and both tags.
