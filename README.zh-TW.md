# 貓砂櫃智慧偵測系統

[English](README.md) | [繁體中文](README.zh-TW.md)

以 Seeed Studio XIAO ESP32-S3 為主控，結合 VL53L0X 距離偵測、XY-134.2K RFID 身分識別與 Google Apps Script/Sheets 記錄貓咪使用貓砂櫃的事件。

目前主線是 RFID + ToF；相機影像辨識為已驗證、暫停發展的實驗方案，仍保留資料收集工具與 Edge Impulse 模型。

## 目錄

- `firmware/main/`：主系統 Arduino sketch（RFID + VL53L0X + Apps Script）
- `firmware/tests/`：單一硬體驗證程式與紀錄
- `backend/`：Google Sheets Apps Script 接收端
- `experiments/vision/`：已歸檔的 ESP32 拍照流程、本機 Flask 收圖工具與 Edge Impulse 模型；Arduino sketch 位於 `esp32_camera_stream/`

本機工程紀錄統一放在已忽略的 `docs/`，不屬於公開專案文件。

## 安全

敏感設定採本機注入，以 `secrets.example.h` 定義介面、`secrets.h` 承載實值，並由 `.gitignore` 排除於版本控制。設備憑證、身分識別原始值與影像資料集皆與原始碼倉庫分離管理。

## 狀態

- 主韌體：已編譯通過，待實機佈線、燒錄與電流量測
- VL53L0X：獨立網頁測距程式已收入 `firmware/tests/`
- RFID：等待模組到貨後依測試清單驗證
- Vision：歸檔，現階段不繼續開發
