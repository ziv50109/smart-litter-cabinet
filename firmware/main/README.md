貓砂櫃智慧偵測系統
==================

【中文】
功能：偵測進出，以 ISO11784/85 晶片辨識並上傳 Google Sheets。
檔案：main.ino 主程式；app_config.h 設定；certificates.h 公開憑證；secrets.example.h 空白範本；secrets.h 僅限本機。
接線：依 Cirkit Designer 電路圖，XY ON/OFF=GPIO1（D0）；ToF INT/SDA/SCL=GPIO2/5/6（D1/D4/D5）；ESP32 TX→XY RXD=GPIO43（D6）；ESP32 RX←XY TXD=GPIO44（D7）；所有模組共地。RFID 的 VCC 不由 GPIO 供電，A1/A2 只接指定天線，模組電源附近保留電解與陶瓷去耦電容。
流程：IDLE→進入→RFID→停留→離開→UPLOAD；RFID/Wi-Fi 平常關閉。實測規則為距離嚴格小於200mm並持續300ms才確認進入，距離大於或等於200mm並持續500ms才確認離開；待機1000ms、活動100ms。
XY：9600 8N1，封包 $F＋15位數字＋XOR＋#。上傳失敗存 NVS（8筆），活動中的事件每30秒備份。
IDE：安裝 ESP32、Pololu VL53L0X，選 XIAO ESP32-S3；只在本機 secrets.h 填秘密，再 Verify、Upload，序列埠115200。

--------------------
SMART LITTER MONITOR
====================

[English]
Detect visits, identify by ISO11784/85 chip, and upload to Google Sheets.
Pins (from the Cirkit Designer circuit): XY ON/OFF=GPIO1 (D0); ToF INT/SDA/SCL=GPIO2/5/6 (D1/D4/D5); ESP32 TX to XY RXD=GPIO43 (D6); ESP32 RX from XY TXD=GPIO44 (D7); all modules share GND. Do not power RFID from a GPIO. Connect A1/A2 only to the specified antenna and keep the electrolytic and ceramic decoupling capacitors near the module supply.
Flow: IDLE→entry→RFID→occupied→exit→UPLOAD; RFID/Wi-Fi normally off. The measured rule is strictly below 200mm for 300ms to confirm entry and 200mm or above for 500ms to confirm exit; idle 1000ms, active 100ms.
XY: 9600 8N1; frame $F+15 digits+XOR+#. Failed uploads use NVS (8); active sessions save every 30s.
IDE: install ESP32/VL53L0X, select XIAO ESP32-S3, create local secrets.h, Verify, Upload, Serial=115200.
