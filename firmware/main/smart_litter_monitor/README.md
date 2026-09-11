貓砂櫃智慧偵測系統
==================

【中文】
功能：偵測進出，以 ISO11784/85 晶片辨識並上傳 Google Sheets。
檔案：smart_litter_monitor.ino 主程式；app_config.h 設定；certificates.h 公開憑證；secrets.example.h 空白範本；secrets.h 僅限本機。
接線：XY ON/OFF=GPIO1；ToF INT/SDA/SCL=2/5/6；UART TX/RX=43/44；共地。
流程：IDLE→進入→RFID→停留→離開→UPLOAD；RFID/Wi-Fi 平常關閉。進入150mm/300ms、離開200mm/500ms、待機1000ms、活動100ms。
XY：9600 8N1，封包 $F＋15位數字＋XOR＋#。上傳失敗存 NVS（8筆），事件每5秒備份。
IDE：安裝 ESP32、Pololu VL53L0X，選 XIAO ESP32-S3；只在本機 secrets.h 填秘密，再 Verify、Upload，序列埠115200。

--------------------
SMART LITTER MONITOR
====================

[English]
Detect visits, identify by ISO11784/85 chip, and upload to Google Sheets.
Pins: XY=1; ToF=2/5/6; UART=43/44; common GND.
Flow: IDLE→entry→RFID→occupied→exit→UPLOAD; RFID/Wi-Fi normally off. Entry 150mm/300ms, exit 200mm/500ms, idle 1000ms, active 100ms.
XY: 9600 8N1; frame $F+15 digits+XOR+#. Failed uploads use NVS (8); sessions save every 5s.
IDE: install ESP32/VL53L0X, select XIAO ESP32-S3, create local secrets.h, Verify, Upload, Serial=115200.
