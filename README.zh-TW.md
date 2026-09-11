# 貓砂櫃智慧偵測系統

[README.md](README.md)

以 Seeed Studio XIAO ESP32-S3 為主控，結合 VL53L0X 距離偵測、XY-134.2K RFID 身分識別與 Google Apps Script/Sheets 記錄貓咪使用貓砂櫃的事件。

目前主線是 RFID + ToF；相機影像辨識為已驗證、暫停發展的實驗方案，仍保留資料收集工具與 Edge Impulse 模型。

## 運作流程

```mermaid
flowchart TD
    Battery[503450 鋰電池] --> Boost[MT3608 升壓至 5V]
    Boost --> ESP32[XIAO ESP32-S3]
    Boost --> RFID[XY_134.2K RFID 讀卡器]
    ESP32 --> ToF[VL53L0X 距離感測器]

    ToF -->|首次讀到小於 200mm| Detect[立即啟動進入掃描與 300ms 防抖]
    Detect -->|開啟讀卡器| RFID
    RFID -->|收到有效 FDX-B 封包| Identify[取得晶片編號與貓咪名稱]
    RFID -->|3秒內未讀到有效資料| Unknown[暫時保留未知身分]
    Identify --> Monitor[記錄距離與停留時間]
    Unknown --> Monitor
    Monitor -->|首次讀到大於等於 200mm| Exit[立即啟動離開掃描與 500ms 防抖]
    Exit -->|距離回到小於 200mm| Monitor
    Exit -->|確認離開且掃描結束| WiFi[保留最後有效身分並上傳]
    WiFi --> Script[Google Apps Script]
    Script --> Sheet[貓砂櫃智慧偵測紀錄]
    Sheet --> Sleep[關閉 Wi-Fi 與 RFID，回到待機]
```

## 目錄

- `firmware/main/`：主系統 Arduino sketch（RFID + VL53L0X + Apps Script）
- `firmware/tests/`：單一硬體驗證程式與紀錄
- `backend/`：Google Sheets Apps Script 接收端
- `hardware/`：硬體採購、電源設計、接線圖與 RFID 實測資料
- `experiments/vision/`：已歸檔的 ESP32 拍照流程、本機 Flask 收圖工具與 Edge Impulse 模型；Arduino sketch 位於 `esp32_camera_stream/`

## 安全

敏感設定採本機注入，以 `secrets.example.h` 定義介面、`secrets.h` 承載實值，並由 `.gitignore` 排除於版本控制。設備憑證、身分識別原始值與影像資料集皆與原始碼倉庫分離管理。

## 狀態

- 主系統：目前韌體已編譯通過；實機接線、5V 供電、ToF、RFID 與 Google Sheets 離線補送均已驗證
- 除錯頁：可選用的區域網路 WebServer，即時顯示 ToF、RFID UART 校驗、狀態、紀錄、Wi-Fi 與上傳佇列
- RFID 辨識：進出首次跨過門檻立即掃描，每次最多 3 秒；離開漏讀沿用進入身分，兩次漏讀才為 unknown。此掃描時機修訂仍待實機驗證。
- VL53L0X：獨立網頁測距程式已收入 `firmware/tests/`
- RFID：130mm 線圈在實際環境中可隔著貓咪皮膚讀取 2×12mm FDX-B 晶片，實測距離約 10–13cm
- Vision：歸檔，現階段不繼續開發
