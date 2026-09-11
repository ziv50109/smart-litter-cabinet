# VL53L0X distance web test

## 中文

從 Arduino IDE 原始 `sketch_aug22b` 另存的硬體驗證程式。XIAO ESP32-S3 使用 GPIO5/SDA、GPIO6/SCL 連接 VL53L0X，感測器連續測距週期為 100 ms，手機網頁每 500 ms 更新。

開啟前複製 `secrets.example.h` 為 `secrets.h` 並填入本機 Wi-Fi。本測試的顯示分類門檻只是調校輔助，不是主系統的進出判斷參數。

---

## English

This hardware-validation sketch was saved from the original Arduino IDE `sketch_aug22b`. On the XIAO ESP32-S3, VL53L0X uses GPIO5/SDA and GPIO6/SCL. The sensor runs a 100 ms continuous-ranging cycle, while the phone webpage refreshes every 500 ms.

Before use, copy `secrets.example.h` to `secrets.h` and set the local Wi-Fi credentials. The displayed distance categories are calibration aids, not the main system's entry/exit thresholds.
