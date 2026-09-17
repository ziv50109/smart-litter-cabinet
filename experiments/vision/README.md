# Vision / TinyML experiment

> Archived experiment. The production system uses RFID for identity and VL53L0X for visit detection.

This directory keeps the camera-based data-collection workflow and the exported Edge Impulse Arduino model so the experiment can still be reproduced without mixing it into the production firmware.

## Layout

```text
vision/
├─ esp32_camera_stream/   # XIAO ESP32-S3 Sense camera UI and JPEG upload
├─ server/                # Local Python/Flask dataset receiver
├─ model/                 # Edge Impulse Arduino library export
└─ README.md
```

## Dataset server

Requirements are defined by `server/pyproject.toml`: Python 3.9+ and Flask 3.1.2.

From `experiments/vision/server` on Windows:

```powershell
py -3.12 -m venv .venv
.venv\Scripts\python -m pip install --upgrade pip
.venv\Scripts\python -m pip install -r requirements.txt
.venv\Scripts\python dataset_server.py
```

The server listens on `0.0.0.0:5000`. Keep it on a trusted LAN; it is a dataset collector, not an Internet-facing service.

Uploaded files are stored under `server/dataset/`, which is ignored by Git. Requests are limited to 10 MB. Accepted dataset labels are `翎角`, `麻嚕`, and `unknown`.

## ESP32 camera collector

1. Copy `esp32_camera_stream/secrets.example.h` to local `secrets.h`.
2. Configure Wi-Fi and the dataset server address.
3. Build `esp32_camera_stream.ino` for XIAO ESP32-S3 Sense.
4. Open the device page from a phone on the same LAN, choose a label, capture a JPEG, and upload it to the server.

The camera sketch collects images only; it does not run the archived model locally.

## Model

`model/ei-cat-classifier-arduino-1.0.5.zip` is the retained Edge Impulse Arduino library export. It contains the inference SDK and model, not the photo dataset. The archived model has two output classes: `翎角` and `麻嚕`; `unknown` is a dataset-collection label, not a model output.

The ESP32 uploader currently builds a complete multipart request buffer in memory for each JPEG, so large captures can increase heap pressure.

---

# 影像／TinyML 實驗

> 已歸檔。正式系統使用 RFID 辨識身分、VL53L0X 偵測使用事件。

此資料夾保留相機資料收集流程與 Edge Impulse 匯出的 Arduino 模型，方便未來重現實驗，但不混入目前正式韌體。

## 目錄

```text
vision/
├─ esp32_camera_stream/   # XIAO ESP32-S3 Sense 相機介面與 JPEG 上傳
├─ server/                # 本機 Python/Flask dataset receiver
├─ model/                 # Edge Impulse Arduino library export
└─ README.md
```

## Dataset server

`server/pyproject.toml` 定義 Python 3.9+ 與 Flask 3.1.2。

Windows 可在 `experiments/vision/server` 執行：

```powershell
py -3.12 -m venv .venv
.venv\Scripts\python -m pip install --upgrade pip
.venv\Scripts\python -m pip install -r requirements.txt
.venv\Scripts\python dataset_server.py
```

Server 監聽 `0.0.0.0:5000`。它只適合信任的區域網路，不應直接暴露到 Internet。

圖片與 `metadata.csv` 存在 `server/dataset/`，該路徑已由 Git 忽略；單次 request 上限 10 MB。允許的資料集 labels 為 `翎角`、`麻嚕`、`unknown`。

## ESP32 相機收集器

1. 複製 `esp32_camera_stream/secrets.example.h` 為本機 `secrets.h`。
2. 設定 Wi-Fi 與 dataset server 位址。
3. 以 XIAO ESP32-S3 Sense 編譯 `esp32_camera_stream.ino`。
4. 手機與裝置位於同一 LAN 時，開啟 ESP32 頁面選 label、拍 JPEG 並上傳。

目前相機 sketch 只負責收集影像，不會在 ESP32 上執行封存模型推論。

## 模型

`model/ei-cat-classifier-arduino-1.0.5.zip` 是保留的 Edge Impulse Arduino library export，包含推論 SDK 與模型，不含照片資料集。模型輸出類別只有 `翎角` 與 `麻嚕`；`unknown` 只用於收集資料，不是模型輸出。

ESP32 上傳 JPEG 時目前會在記憶體建立完整 multipart request buffer，因此較大的圖片會增加 heap 壓力。
