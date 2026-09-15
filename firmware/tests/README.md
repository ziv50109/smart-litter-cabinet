
# Hardware validation tests

Each directory isolates a component or a specific hardware interaction and records the sketch version, wiring, measured results, and acceptance criteria. This makes it easier to separate sensor failures from main-firmware logic issues.

1. `vl53l0x_distance_web`: distance, timeout behavior, doorway resolution, and one-second polling.
2. `xy134-rfid-reader`: UART frame, checksum, read range, repeated scans, and both tags.
3. [`battery_probe`](battery_probe/README.md): offline gated-RFID / Light-sleep A/B verification, persistent raw entrance trace, and host characterization of the existing short-visit timeout. **Draft; not a production logger or a validated battery fix.**

---

# 硬體驗證測試

每個資料夾隔離驗證元件或明確的硬體互動，並保留「程式版本、接線、實測結果、通過條件」，避免主韌體問題時無法分辨是感測器還是系統邏輯。

1. `vl53l0x_distance_web`：距離、timeout、門口解析度與 1 秒輪詢。
2. `xy134-rfid-reader`：UART frame、checksum、讀取距離、重複掃描與兩個標籤。
3. [`battery_probe`](battery_probe/README.md)：離線按需 RFID／Light-sleep A/B 驗證、可於重啟後讀回的入口原始軌跡，以及現有短訪逾時問題的主機重現。**待實機驗收，不是正式 logger，也不是已驗證的電池修復。**
