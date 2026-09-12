# Smart Litter Cabinet

[繁體中文版](README.zh-TW.md)

An event-monitoring system built around the Seeed Studio XIAO ESP32-S3. It combines VL53L0X distance sensing, XY-134.2K RFID identification, and Google Apps Script/Sheets to record litter-cabinet visits.

The current design uses RFID and ToF. Camera-based recognition is an archived experiment; its data-capture utilities and trained Edge Impulse model remain available for reference.

## How it works

```mermaid
flowchart TD
    Battery[503450 LiPo] --> Boost[MT3608 boosts to 5V]
    Boost -->|USB-C| ESP32[XIAO ESP32-S3]
    ESP32 -->|Board 5V| RFID[XY_134.2K RFID]
    ESP32 -->|Board 3.3V| ToF[VL53L0X]
    ToF -->|First reading below 200mm| Entry[Entry activity: start event and RFID]
    Entry -->|Continuously valid clear for 10s| Inside[Presumed inside: retain identity]
    Inside -->|Below 200mm again| Exit[Exit candidate: scan RFID again]
    Exit -->|Clear for 10s and scan complete| Close[Completed snapshot]
    Entry -->|No exit candidate by 90s| Close
    Inside -->|No exit candidate by 90s| Close
    Exit -->|Still unconfirmed at 100s| Close
    Close -->|Identity conflict| Drop[Discard without upload]
    Close -->|No conflict| Queue[NVS pending queue]
    Queue --> Worker[Background HTTPS delivery to Google Sheets]
```

## Structure

- `firmware/main/`: main RFID + VL53L0X + Apps Script Arduino sketch
- `firmware/tests/`: isolated hardware validation sketches and records
- `backend/`: Google Sheets Apps Script receiver
- `hardware/`: component sourcing, power design, wiring diagram, and measured RFID range
- `experiments/vision/`: archived camera capture workflow, local Flask collector, and Edge Impulse model; its Arduino sketch is under `esp32_camera_stream/`

## Security

Sensitive configuration is injected locally: `secrets.example.h` defines the interface, while ignored `secrets.h` files provide deployment values. Device credentials, raw identity values, and image datasets are managed separately from source control.

## Status

- Main system: the revised event logic still requires hardware validation; compilation and simulation do not establish real cat-visit accuracy.
- Debug dashboard: optional local-only WebServer shows live ToF, RFID UART validation, state, session, Wi-Fi, and upload queue data
- RFID identification: up to 10 seconds per scan; misses preserve identity and conflicts are not uploaded. See [firmware rules](firmware/main/README.md).
- VL53L0X: standalone web distance test is under `firmware/tests/`
- RFID: a 130mm coil reads the implanted 2×12mm FDX-B chip at approximately 10–13cm in the installed test environment
- Vision: archived and not part of the current MVP
