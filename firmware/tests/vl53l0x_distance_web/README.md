# VL53L0X distance web test

Standalone XIAO ESP32-S3 hardware test for the VL53L0X on GPIO5/SDA and GPIO6/SCL. The sensor uses 100 ms continuous ranging and the phone page refreshes every 500 ms.

Copy `secrets.example.h` to local `secrets.h`, set Wi-Fi credentials, build `vl53l0x_distance_web.ino`, and open the IP printed by Serial Monitor.

The webpage's distance labels are calibration aids only; the production firmware uses its own `<200 mm` doorway threshold.

---

# VL53L0X 網頁測距測試

XIAO ESP32-S3 的 VL53L0X 獨立硬體測試，SDA 使用 GPIO5、SCL 使用 GPIO6。感測器以 100 ms continuous ranging 運作，手機頁面每 500 ms 更新一次。

複製 `secrets.example.h` 為本機 `secrets.h` 並填入 Wi-Fi，編譯 `vl53l0x_distance_web.ino` 後，使用 Serial Monitor 顯示的 IP 開啟頁面。

網頁上的距離分類只用於調校；正式韌體的入口門檻另外定義為 `<200 mm`。
