#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TinyML Dataset Server
接收 ESP32-S3 上傳的貓咪照片，依 Label + 時間戳記命名存檔，並記錄到 metadata.csv

安裝需求：
    pip install flask

執行方式：
    python dataset_server.py

執行後會顯示監聽的網址，例如 http://0.0.0.0:5000
請確認 ESP32 程式碼裡的 pc_server_ip 設成這台電腦在區網內的實際 IP（用 ipconfig 查詢）
"""

from __future__ import print_function

import sys

if sys.version_info < (3, 9):
    sys.stderr.write("Python 3.9 or newer is required. Python 2 is not supported.\n")
    raise SystemExit(2)

import threading
import uuid
from pathlib import Path
from flask import Flask, request, jsonify
import csv
from datetime import datetime

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 10 * 1024 * 1024

SERVER_DIR = Path(__file__).resolve().parent
DATASET_PATH = (SERVER_DIR / "dataset").resolve()
DATASET_DIR = str(DATASET_PATH)
METADATA_FILE = str(DATASET_PATH / "metadata.csv")
METADATA_LOCK = threading.Lock()
ALLOWED_LABELS = {"翎角", "麻嚕", "unknown"}

def ensure_dataset_ready():
    DATASET_PATH.mkdir(parents=True, exist_ok=True)
    if not Path(METADATA_FILE).exists():
        with open(METADATA_FILE, "w", newline="", encoding="utf-8-sig") as f:
            csv.writer(f).writerow(["filename", "label", "timestamp"])


def sanitize_label(label):
    return label if label in ALLOWED_LABELS else "unknown"


def build_filename(label, now):
    return "{}_{}_{}.jpg".format(
        label, now.strftime("%Y%m%d%H%M%S%f"), uuid.uuid4().hex
    )


@app.route("/upload", methods=["POST"])
def upload():
    label = sanitize_label(request.form.get("label", "unknown"))
    file = request.files.get("file")

    if file is None:
        return jsonify(status="error", message="no file received"), 400

    now = datetime.now()
    # Labels are allow-listed above, and the resolved path must remain under dataset/.
    label_dir = (DATASET_PATH / label).resolve()
    if DATASET_PATH not in label_dir.parents:
        return jsonify(status="error", message="invalid label path"), 400
    label_dir.mkdir(parents=True, exist_ok=True)
    filename = build_filename(label, now)
    filepath = (label_dir / filename).resolve()
    if DATASET_PATH not in filepath.parents:
        return jsonify(status="error", message="invalid file path"), 400

    with METADATA_LOCK:
        file.save(str(filepath))
        with open(METADATA_FILE, "a", newline="", encoding="utf-8-sig") as f:
            csv.writer(f).writerow(["{}/{}".format(label, filename), label, now.strftime("%Y-%m-%d %H:%M:%S")])

        label_count = 0
        with open(METADATA_FILE, newline="", encoding="utf-8-sig") as f:
            label_count = sum(1 for row in csv.DictReader(f) if row["label"] == label)

    print("[已接收] {}  (label={}, 累積 {} 張)".format(filename, label, label_count))
    return jsonify(status="ok", filename=filename, label=label, count=label_count)


@app.route("/", methods=["GET"])
def index():
    ensure_dataset_ready()
    with open(METADATA_FILE, encoding="utf-8-sig") as f:
        count = sum(1 for _ in f) - 1
    return "Dataset Server 運作中。目前已收到 {} 張照片。".format(max(count, 0))


if __name__ == "__main__":
    ensure_dataset_ready()
    print("Dataset 存放路徑: {}".format(DATASET_DIR))
    print("Metadata 檔案: {}".format(METADATA_FILE))
    print("啟動伺服器中，監聽 0.0.0.0:5000 ...")
    app.run(host="0.0.0.0", port=5000, debug=False)
