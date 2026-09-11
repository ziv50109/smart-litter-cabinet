# Smart Litter Cabinet

[繁體中文版](README.zh-TW.md)

An event-monitoring system built around the Seeed Studio XIAO ESP32-S3. It combines VL53L0X distance sensing, XY-134.2K RFID identification, and Google Apps Script/Sheets to record litter-cabinet visits.

The current design uses RFID and ToF. Camera-based recognition is an archived experiment; its data-capture utilities and trained Edge Impulse model remain available for reference.

## How it works

```mermaid
flowchart TD
    Battery[503450 LiPo battery] --> Boost[MT3608 boosts to 5V]
    Boost --> ESP32[XIAO ESP32-S3]
    Boost --> RFID[XY_134.2K RFID reader]
    ESP32 --> ToF[VL53L0X distance sensor]

    ToF -->|First reading below 200mm| Detect[Start entry scan and 300ms debounce together]
    Detect -->|Enable reader| RFID
    RFID -->|Valid FDX-B frame| Identify[Resolve chip ID and cat]
    RFID -->|No valid read within 3 seconds| Unknown[Keep identity provisionally unknown]
    Identify --> Monitor[Track distance and visit duration]
    Unknown --> Monitor
    Monitor -->|First reading at least 200mm| Exit[Start exit scan and 500ms debounce together]
    Exit -->|Distance returns below 200mm| Monitor
    Exit -->|Confirmed exit and scan complete| WiFi[Keep last valid identity and upload]
    WiFi --> Script[Google Apps Script]
    Script --> Sheet[Smart litter cabinet log]
    Sheet --> Sleep[Disable Wi-Fi and RFID; return to idle]
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

- Main system: the current firmware compiles; wiring, 5V power, ToF, RFID, and offline delivery to Google Sheets have been validated on hardware
- Debug dashboard: optional local-only WebServer shows live ToF, RFID UART validation, state, session, Wi-Fi, and upload queue data
- RFID identification: starts immediately at each entry/exit threshold crossing, up to three seconds per scan; a missed exit read retains the entry identity. Both misses leave unknown. This timing revision still requires hardware validation.
- VL53L0X: standalone web distance test is under `firmware/tests/`
- RFID: a 130mm coil reads the implanted 2×12mm FDX-B chip at approximately 10–13cm in the installed test environment
- Vision: archived and not part of the current MVP
