# Smart Litter Cabinet

[English](README.md) | [繁體中文](README.zh-TW.md)

An event-monitoring system built around the Seeed Studio XIAO ESP32-S3. It combines VL53L0X distance sensing, XY-134.2K RFID identification, and Google Apps Script/Sheets to record litter-cabinet visits.

The current design uses RFID and ToF. Camera-based recognition is an archived experiment; its data-capture utilities and trained Edge Impulse model remain available for reference.

## Structure

- `firmware/main/`: main RFID + VL53L0X + Apps Script Arduino sketch
- `firmware/tests/`: isolated hardware validation sketches and records
- `backend/`: Google Sheets Apps Script receiver
- `experiments/vision/`: archived camera capture workflow, local Flask collector, and Edge Impulse model; its Arduino sketch is under `esp32_camera_stream/`

Local engineering notes are kept under the ignored `docs/` directory and are not part of the published repository.

## Security

Sensitive configuration is injected locally: `secrets.example.h` defines the interface, while ignored `secrets.h` files provide deployment values. Device credentials, raw identity values, and image datasets are managed separately from source control.

## Status

- Main firmware: compiles successfully; physical wiring, flashing, and current measurement remain
- VL53L0X: standalone web distance test is under `firmware/tests/`
- RFID: validation starts when the reader module arrives
- Vision: archived and not part of the current MVP
