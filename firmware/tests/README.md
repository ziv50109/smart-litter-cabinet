# Hardware validation tests

Each directory isolates a component or specific hardware interaction so sensor failures can be separated from main-firmware logic issues.

1. `vl53l0x_distance_web`: distance readings, timeout behavior, doorway resolution, and the phone web view.
2. `xy134-rfid-reader`: UART frames, checksum validation, read range, repeated scans, and both tags.
3. [`battery_probe`](battery_probe/README.md): offline gated-RFID / Light-sleep A/B verification and persistent entrance traces.
4. [`auto_probe`](auto_probe/README.md): phone-driven low-power capture with STA/AP Web UI and OTA.
5. `main_host`: pure C++ regression tests for the production visit state machine, including short visits and sample-gap safety.

---

# 硬體驗證測試

每個資料夾各自驗證一項元件或硬體互動，方便在主韌體發生問題時區分感測器、通訊與狀態機邏輯。

1. `vl53l0x_distance_web`：距離讀值、逾時行為、入口解析度與手機網頁顯示。
2. `xy134-rfid-reader`：UART 封包、checksum、讀取距離、重複掃描與兩個標籤。
3. [`battery_probe`](battery_probe/README.md)：離線按需 RFID／Light-sleep A/B 驗證與入口原始追蹤資料。
4. [`auto_probe`](auto_probe/README.md)：以手機操作的低功耗測試、STA/AP Web UI 與 OTA。
5. `main_host`：正式狀態機的純 C++ 回歸測試，涵蓋短事件、RFID 逾時、取樣空窗與無效取樣。
