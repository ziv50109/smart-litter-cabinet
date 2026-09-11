# 影像／TinyML 實驗

> 狀態：已歸檔。正式系統使用 RFID 辨識身分、VL53L0X 偵測停留；本模組保留供日後重現與評估。

### 範圍

本模組整合三個相依成果：XIAO ESP32-S3 Sense 相機取像介面、本機 Flask 資料集接收器，以及 Edge Impulse 匯出的 TinyML Arduino 模型。相機程式可透過手機網頁選擇貓咪 label 或 `unknown` 後拍照，再將 JPEG 傳至同一區域網路內的電腦。現有 `esp32_camera_stream.ino` 尚未直接整合模型推論；模型 ZIP 保留為可重現的獨立匯出物。

### 目錄

```text
vision/
├─ esp32_camera_stream/   # 相機串流、label 選擇與照片上傳
├─ server/                # Python/Flask 本機接收器
├─ model/                 # Edge Impulse Arduino library export
└─ README.md
```

### 環境基準

| 元件 | 版本／需求 | 狀態 |
|---|---|---|
| 作業系統 | Windows 10/11；其他平台需自行調整指令與防火牆 | Windows 流程已文件化 |
| Python | 3.9 以上；建議 3.12 | Python 2 明確不支援；3.12 已通過語法檢查 |
| Flask | `3.1.2` | `requirements.txt` 與 `pyproject.toml` 固定相同版本 |
| Arduino IDE | 2.3.10 | 本機使用版本 |
| Arduino-ESP32 core | 3.3.11 | 相機 Sketch 已以 XIAO ESP32-S3 toolchain 編譯通過 |
| 開發板 | Seeed Studio XIAO ESP32-S3 Sense | 目標硬體 |
| Edge Impulse export | Arduino library 1.0.5；Studio project 1093822 | 原始模型封存 |

Flask 3.1 官方要求 Python 3.9 以上，因此本專案不提供 Python 2 相容模式。版本依據：[Flask Installation](https://flask.palletsprojects.com/en/stable/installation/)。

### Windows 啟動方式

在 `experiments/vision/server` 執行：

```powershell
py -3.12 -m venv .venv
.venv\Scripts\python -m pip install --upgrade pip
.venv\Scripts\python -m pip install -r requirements.txt
.venv\Scripts\python dataset_server.py
```

服務預設監聽 `0.0.0.0:5000`。先確認 Windows 防火牆只允許信任的私人網路，再將電腦 LAN IP 寫入本機 `esp32_camera_stream/secrets.h`。此 Flask server 是區網資料收集工具，不應直接暴露到網際網路。

### ESP32 設定

1. 將 `esp32_camera_stream/secrets.example.h` 複製為同資料夾內的本機 `secrets.h`。
2. 設定 Wi-Fi、電腦 LAN IP 與連接埠。
3. Arduino IDE 選擇 XIAO ESP32-S3，先 Verify，再 Upload。
4. 從 Serial Monitor 取得裝置 IP，以同網段手機開啟頁面並測試三種 labels。

### 資料與模型

- 收到的圖片與 `metadata.csv` 固定產生在 `server/dataset/`（以 `dataset_server.py` 所在目錄為基準），已由 Git 忽略；單次 multipart 請求上限約 10 MB。
- 真實照片資料集保留在 Git 外，不隨倉庫發布。
- `model/ei-cat-classifier-arduino-1.0.5.zip` 是 Edge Impulse 專案的原始 Arduino library export，內含推論 SDK 與模型，不含訓練照片。
- 舊資料曾出現不一致的 Maru 中文 label；新增資料前應先固定 label schema，避免類別被拆分。
- 收集器允許 `翎角`、`麻嚕`、`unknown` 三種資料集 label；目前 ZIP 模型只有 `翎角`、`麻嚕` 兩個推論類別，`unknown` 不代表模型輸出類別。
- ESP32 目前仍會配置一份完整 multipart buffer 來上傳 JPEG；這保留為後續低風險優化項目，硬體驗證時需留意堆積碎片與大照片配置失敗。

### 驗證狀態

- Python server：Python 3.12 語法檢查完成。
- Python server：3 個單元測試已在 Flask 3.1.2 環境通過。
- 模型 ZIP：完整性與敏感字串掃描完成；未含照片、Wi-Fi、Apps Script 資訊或晶片原始號碼。
- ESP32 相機 Sketch：已以 Arduino-ESP32 3.3.11 的 XIAO ESP32-S3 toolchain 編譯通過。
- ESP32 相機上傳與 TinyML 推論：整理後尚未重新進行端到端硬體驗證。

---

# Vision / TinyML experiment

> Status: archived. The production system uses RFID for identity and VL53L0X for visit detection; this module remains reproducible for future evaluation.

### Scope

This module groups three related artifacts: the XIAO ESP32-S3 Sense camera interface, the local Flask dataset receiver, and the TinyML Arduino model exported from Edge Impulse. A phone can open the ESP32-hosted page, select a cat label or `unknown`, capture a JPEG, and upload it to a computer on the same LAN. The current `esp32_camera_stream.ino` does not yet run model inference directly; the model ZIP is retained as a reproducible standalone export.

### Layout

```text
vision/
├─ esp32_camera_stream/   # Camera stream, label selection, and upload
├─ server/                # Local Python/Flask receiver
├─ model/                 # Edge Impulse Arduino library export
└─ README.md
```

### Environment baseline

| Component | Version / requirement | Status |
|---|---|---|
| Operating system | Windows 10/11; adapt commands and firewall rules for other systems | Windows workflow documented |
| Python | 3.9 or newer; 3.12 recommended | Python 2 is unsupported; syntax checked with 3.12 |
| Flask | `3.1.2` | The same exact version is pinned in both files |
| Arduino IDE | 2.3.10 | Version used locally |
| Arduino-ESP32 core | 3.3.11 | Camera sketch compiles with the XIAO ESP32-S3 toolchain |
| Board | Seeed Studio XIAO ESP32-S3 Sense | Target hardware |
| Edge Impulse export | Arduino library 1.0.5; Studio project 1093822 | Original model archive |

Flask 3.1 requires Python 3.9 or newer, so this project intentionally provides no Python 2 compatibility mode. See the [official Flask installation documentation](https://flask.palletsprojects.com/en/stable/installation/).

### Windows setup

Run the following from `experiments/vision/server`:

```powershell
py -3.12 -m venv .venv
.venv\Scripts\python -m pip install --upgrade pip
.venv\Scripts\python -m pip install -r requirements.txt
.venv\Scripts\python dataset_server.py
```

The service listens on `0.0.0.0:5000` by default. Restrict the Windows Firewall rule to a trusted private network, then place the computer's LAN IP in the local `esp32_camera_stream/secrets.h`. This Flask server is a LAN dataset tool and must not be exposed directly to the Internet.

### ESP32 setup

1. Copy `esp32_camera_stream/secrets.example.h` to a local `secrets.h` in the same directory.
2. Configure Wi-Fi, the computer LAN IP, and the port.
3. Select XIAO ESP32-S3 in Arduino IDE, run Verify, then Upload.
4. Read the device IP from Serial Monitor and test all three labels from a phone on the same network.

### Data and model

- The real photo dataset remains outside the repository.
- The receiver stores images and `metadata.csv` under `server/dataset/`, anchored to the script directory (not the process working directory), and limits each request to about 10 MB.
- `model/ei-cat-classifier-arduino-1.0.5.zip` is the original Edge Impulse Arduino library export. It contains the inference SDK and model, but no training photos.
- Historical data used inconsistent Chinese spellings for Maru. Define one stable label schema before collecting more images.
- The collector accepts three dataset labels: `翎角`, `麻嚕`, and `unknown`. The current ZIP model has only two inference classes, `翎角` and `麻嚕`; `unknown` is not a model output class.
- The ESP32 still allocates one complete multipart buffer for each JPEG upload. This is documented as a follow-up optimization; hardware validation should watch for heap fragmentation and allocation failures with large images.

### Validation status

- Python server: syntax checked with Python 3.12.
- Python server: all three unit tests pass with Flask 3.1.2.
- Model ZIP: integrity and sensitive-string scan completed; no photos, Wi-Fi values, Apps Script data, or raw chip numbers were found.
- ESP32 camera sketch: compiles with the Arduino-ESP32 3.3.11 XIAO ESP32-S3 toolchain.
- ESP32 camera upload and TinyML inference: not yet revalidated end to end after this reorganization.
