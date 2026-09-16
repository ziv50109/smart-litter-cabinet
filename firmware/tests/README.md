# Hardware validation tests

Each directory isolates a component or a specific hardware interaction and records the sketch version, wiring, measured results, and acceptance criteria. This makes it easier to separate sensor failures from main-firmware logic issues.

1. `vl53l0x_distance_web`: distance, timeout behavior, doorway resolution, and one-second polling.
2. `xy134-rfid-reader`: UART frame, checksum, read range, repeated scans, and both tags.
3. [`battery_probe`](battery_probe/README.md): offline gated-RFID / Light-sleep A/B verification and persistent raw entrance trace.
4. [`auto_probe`](auto_probe/README.md): phone-friendly automatic low-power capture with STA/AP Web UI and OTA.
5. `main_host`: pure C++ regression tests for the production visit state machine, including short visits and sample-gap safety.

---

# 硬體驗證測試

每個資料夾隔離驗證元件或明確的硬體互動，並保留「程式版本、接線、實測結果、通過條件」，避免主韌體問題時無法分辨是感測器還是系統邏輯。

1. `vl53l0x_distance_web`：距離、timeout、門口解析度與 1 秒輪詢。
2. `xy134-rfid-reader`：UART frame、checksum、讀取距離、重複掃描與兩個標籤。
3. [`battery_probe`](battery_probe/README.md)：離線按需 RFID／Light-sleep A/B 驗證與入口原始軌跡。
4. [`auto_probe`](auto_probe/README.md)：適合手機操作的自動低功耗 capture、STA/AP Web UI 與 OTA。
5. `main_host`：正式 visit state machine 的純 C++ regression，包含短訪問、RFID timeout、長取樣空窗與 invalid sample 不得誤算 clear。
