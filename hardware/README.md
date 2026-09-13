
# Hardware sourcing, wiring, and measurements

This page is the hardware specification owner. Match voltage, interface, frequency, and pin labels when sourcing; appearance alone is insufficient. Use both the [wiring image](xy134-wiring.png) and the table below. The table is intended to remain sufficient if the image is unavailable.

## Bill of materials and search terms

| Qty. | Component | Suggested search terms | Required properties |
|---:|---|---|---|
| 1 | Seeed Studio XIAO ESP32-S3 | `Seeed Studio XIAO ESP32-S3` | Supported by Arduino-ESP32; exact target board used here |
| 1 | XY_134.2K animal-chip reader | `XY_134.2K 134.2K long range animal chip ISO11784/85 FDX-B UART TTL` | 3.3–6V supply, 3.3V TTL UART, 9600 8N1, 134.2kHz FDX-B; avoid RS485, Wiegand, or 125kHz access-control variants |
| 1 | 130mm circular RFID coil | `130mm 134.2kHz RFID coil 92-102uH` | Connects to A1/A2; recommended inductance 92–102µH |
| 1 | VL53L0X ToF module | `VL53L0X GY-530 I2C ToF distance sensor` | I2C; this project uses GPIO5/GPIO6 |
| 1 | MT3608 boost converter | `MT3608 DC-DC boost converter` | Stable 5V from a single-cell LiPo; not a charger or battery-protection board |
| 1 | 503450 LiPo battery | `503450 3.7V 1000mAh protected LiPo` | 3.7V, 1000mAh, protected and traceable product |
| 1 each | RFID decoupling | `1000uF electrolytic capacitor`, `100nF ceramic capacitor` | Place near RFID VCC/GND and observe electrolytic polarity |

## Power architecture

`503450 3.7V 1000mAh LiPo → MT3608 boosted to 5V → USB Type-C → XIAO ESP32-S3`

The XY_134.2K uses a 5V supply and 3.3V TTL UART. ESP32, VL53L0X, RFID, and the power module must share ground. MT3608 provides neither charging nor undervoltage protection. Before programming, disconnect MT3608 Type-C power and use computer USB alone to prevent two 5V sources from back-feeding each other.

## Wiring

| Module pin | Connection | Notes |
|---|---|---|
| XY ON/OFF | XIAO D0 / GPIO1 | HIGH on, LOW off |
| XY VCC | 5V rail | Never power from a GPIO |
| XY GND | Common ground | Shared with ESP32, sensor, and MT3608 |
| XY RXD | XIAO D6 / GPIO43 TX | ESP32 transmit direction; retained although the main firmware only needs reader output |
| XY TXD | XIAO D7 / GPIO44 RX | RFID data to ESP32 |
| XY A1/A2 | Two ends of the 130mm coil | No polarity; keep leads short and away from metal |
| VL53L0X SDA | XIAO D4 / GPIO5 | I2C data |
| VL53L0X SCL | XIAO D5 / GPIO6 | I2C clock |
| VL53L0X INT | XIAO D1 / GPIO2 | Wired and reserved for later interrupt-wake tuning |

## XY_134.2K vendor data and measured range

The vendor specifies 3.3–6V operation, approximately 90mA at 5V, 3.3V TTL UART, and 9600 8N1. Frames use `$F`, a 15-digit decimal FDX-B number, a two-digit XOR checksum, and `#`.

Only the 2×12mm injection tag relevant to this project is listed:

| Test source and conditions | 65mm circular coil | 130mm circular coil | 24×38cm rectangular coil |
|---|---:|---:|---:|
| Vendor: bare tag at 5V | 12cm | 20cm | 25cm |
| This project: chip implanted under cat skin | Not tested | About 10–13cm | Not tested |

These results use different conditions and do not by themselves indicate degraded performance. Implant orientation, cat posture, coil angle, nearby metal, and mounting position affect range. Mechanical design should use the measured 10–13cm range rather than treating the vendor's bare-tag 20cm figure as a guarantee.

## Initial power-up and troubleshooting

1. Before attaching ESP32, adjust MT3608 to a stable 5V and verify polarity.
2. Confirm common ground, crossed UART TX/RX, and electrolytic-capacitor polarity.
3. Use `firmware/tests/xy134-rfid-reader/` to inspect frames and checksums without publishing real chip numbers.
4. Use `firmware/tests/vl53l0x_distance_web/` to verify detection strictly below 200mm.
5. Finally upload `firmware/main/` and inspect 115200-baud startup output.

If RFID produces no data, check 5V, ground, ON/OFF HIGH, and XY TXD to ESP32 RX. For short range, check metal, converter noise, coil inductance, long A1/A2 leads, and coil/tag orientation. The vendor PDF is retained only for local specification checking because redistribution permission has not been established.

---

# 硬體採購、接線與實測資料

本頁是專案的硬體規格來源。採購時不要只看商品外觀，必須核對工作電壓、通訊介面、頻率與接腳標示。完整接線請同時參考 [接線圖](xy134-wiring.png) 與下方文字表格；即使圖片無法開啟，文字表格仍應足以重新接線。

## 採購清單與搜尋關鍵字

| 數量 | 元件 | 建議搜尋關鍵字 | 必要條件 |
|---:|---|---|---|
| 1 | Seeed Studio XIAO ESP32-S3 | `Seeed Studio XIAO ESP32-S3` | Arduino-ESP32 支援；本專案目標板為 XIAO ESP32-S3 |
| 1 | XY_134.2K 動物晶片讀卡模組 | `XY_134.2K 134.2K 遠距離動物晶片讀卡 ISO11784/85 FDX-B UART TTL` | 3.3–6V 供電、3.3V TTL UART、9600 8N1、支援 134.2kHz FDX-B；不要買成 RS485、Wiegand 或只有 125kHz 的門禁版本 |
| 1 | 130mm RFID 圓形線圈 | `130mm 134.2kHz RFID 線圈 92-102uH` | 連接 A1/A2，建議電感 92–102µH |
| 1 | VL53L0X ToF 模組 | `VL53L0X GY-530 I2C ToF 距離感測器` | I2C 介面；本專案使用 GPIO5/GPIO6 |
| 1 | MT3608 升壓模組 | `MT3608 DC-DC 升壓模組` | 將單節鋰電池升至穩定 5V；它不是充電器，也不等同鋰電池保護板 |
| 1 | 503450 聚合物鋰電池 | `503450 3.7V 1000mAh 台灣商檢合格 聚合物鋰電池` | 3.7V、1000mAh；使用有保護電路且來源可追溯的產品 |
| 各 1 | RFID 電源去耦 | `1000uF 電解電容`、`100nF 陶瓷電容` | 靠近 RFID VCC/GND；電解電容須注意極性 |

## 電源架構

`503450 3.7V 1000mAh 鋰電池 → MT3608 升壓至 5V → USB Type-C → XIAO ESP32-S3`

XY_134.2K 使用 5V 供電，UART 訊號為 3.3V TTL；ESP32、VL53L0X、RFID 與電源模組必須共地。MT3608 只負責升壓，不提供鋰電池充電或低電壓保護。燒錄時先拔除 MT3608 的 Type-C 供電，再接電腦 USB，避免兩個 5V 電源互相回灌。

## 接線表

| 元件接腳 | 連接位置 | 說明 |
|---|---|---|
| XY ON/OFF | XIAO D0 / GPIO1 | HIGH 開啟、LOW 關閉 |
| XY VCC | 5V 電源軌 | 不可由 GPIO 供電 |
| XY GND | 共地 | 與 ESP32、感測器及 MT3608 共地 |
| XY RXD | XIAO D6 / GPIO43 TX | ESP32 傳送方向；目前主韌體只需要接收讀卡資料，但接線保留 |
| XY TXD | XIAO D7 / GPIO44 RX | RFID 傳送資料至 ESP32 |
| XY A1/A2 | 130mm 線圈兩端 | 導線保持短且遠離金屬；線圈本身無極性 |
| VL53L0X SDA | XIAO D4 / GPIO5 | I2C 資料 |
| VL53L0X SCL | XIAO D5 / GPIO6 | I2C 時脈 |
| VL53L0X INT | XIAO D1 / GPIO2 | 已接線，保留供後續中斷喚醒調校 |

## XY_134.2K 廠商資料與實測

廠商規格書列出的工作電壓為 3.3–6V、工作電流約 90mA@5V、通訊介面為 3.3V TTL UART。資料格式為 `$F`、15 位十進位 FDX-B 卡號、兩位 XOR 校驗碼與 `#`，鮑率為 9600、8N1。

讀距只摘錄本專案使用的 **2×12mm 注射標籤**：

| 測試來源與條件 | 65mm 圓形線圈 | 130mm 圓形線圈 | 24×38cm 矩形線圈 |
|---|---:|---:|---:|
| 廠商資料：5V 供電裸測 | 12cm | 20cm | 25cm |
| 本專案：晶片植入貓咪皮下 | 未測 | 約 10–13cm | 未測 |

兩組數據的條件不同，不可直接視為性能衰退。皮下注射環境會受到晶片方向、貓咪姿勢、線圈與身體的相對角度、附近金屬以及安裝位置影響；本專案的機構設計應以實測 10–13cm 為準，不以裸測 20cm 作保證。

## 初次上電與故障排除

1. 未接 ESP32 前先調整 MT3608，確認輸出穩定在 5V並核對正負極。
2. 確認所有模組共地、UART TX/RX 交叉連接，電解電容方向正確。
3. 先使用 `firmware/tests/xy134-rfid-reader/` 查看原始封包及校驗結果；公開截圖或紀錄不得包含真實晶片號碼。
4. 再使用 `firmware/tests/vl53l0x_distance_web/` 確認嚴格小於 200mm 時能偵測貓咪經過。
5. 最後燒錄 `firmware/main/`，以 115200 baud 查看啟動訊息。

完全沒有 RFID 資料時，依序檢查 5V、共地、ON/OFF 是否為 HIGH，以及 RFID TXD 是否接到 ESP32 RX。讀距過短時，先排除金屬、DC-DC 雜訊、線圈電感不符、A1/A2 導線過長及線圈與晶片方向不佳。廠商 PDF 用於本機核對規格，因未確認再散布授權，不直接收錄於公開 repository。
