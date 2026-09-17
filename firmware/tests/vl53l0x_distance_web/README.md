# VL53L0X distance web test

Standalone XIAO ESP32-S3 hardware test for viewing VL53L0X distance readings from a phone. Wiring follows the project hardware documentation.

Copy `secrets.example.h` to local `secrets.h`, set Wi-Fi credentials, build `vl53l0x_distance_web.ino`, and open the IP printed by Serial Monitor.

Sampling, webpage refresh, and display-classification values belong to this test sketch and are not duplicated here. The webpage's distance labels are calibration aids only; production doorway detection uses `Config::ENTRY_THRESHOLD_MM` from `firmware/main/app_config.h`.

---

# VL53L0X 網頁測距測試

以 XIAO ESP32-S3 獨立測試 VL53L0X，並從手機網頁查看即時距離；接線以專案硬體文件為準。

複製 `secrets.example.h` 為本機 `secrets.h` 並填入 Wi-Fi，編譯 `vl53l0x_distance_web.ino` 後，使用 Serial Monitor 顯示的 IP 開啟頁面。

測距週期、網頁更新頻率與畫面上的距離分類都屬於這支測試程式，不在 README 重複數值。網頁分類只用於調校；正式韌體的入口門檻以 `firmware/main/app_config.h` 的 `Config::ENTRY_THRESHOLD_MM` 為準。
