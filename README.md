# Smart Litter Cabinet

[繁體中文版](README.zh-TW.md)

A litter-cabinet visit monitor built around the Seeed Studio XIAO ESP32-S3. The main system uses VL53L0X distance sensing for doorway detection, XY-134.2K RFID for cat identification, and Google Apps Script/Sheets for visit records.

Camera-based recognition remains under `experiments/vision/` as an archived experiment and does not participate in the current visit-detection flow.

## How it works

```mermaid
flowchart TD
    Power[Battery → MT3608 5V → XIAO ESP32-S3] --> Sensors[VL53L0X distance + XY-134.2K RFID]
    Sensors -->|Distance < 200 mm| Entry[Start provisional visit + RFID scan]
    Entry -->|Doorway clear for at least 250 ms| Armed[Arm exit detection]
    Armed -->|Next distance < 200 mm| Exit[Create exit candidate + RFID scan if needed]
    Exit -->|Doorway clear for at least 1 s and RFID scan finished| Resolve[Complete normal visit]
    Entry -->|No confirmed exit by 5 min| Diagnostic[Keep diagnostics only]
    Exit -->|Still unresolved at 5 min 10 s| Diagnostic
    Resolve -->|Registered identity and no conflict| Queue[Persist pending record]
    Resolve -->|No registered identity or identity conflict| Diagnostic
    Queue --> Sheets[Background HTTPS → Google Sheets]
```

The recorded duration is the interval between the first doorway blockage and the second blockage that creates the exit candidate. RFID and network wait time are not added to the visit duration.

## Structure

- `firmware/main/`: production RFID + VL53L0X firmware
- `firmware/tests/`: isolated hardware and state-machine validation tools
- `backend/`: Google Apps Script receiver for Google Sheets
- `hardware/`: sourcing, power, wiring, and measured RFID range
- `experiments/vision/`: archived camera/TinyML experiment

## Current behavior

- VL53L0X is sampled every 200 ms while idle and every 100 ms during a visit.
- RFID is powered only during scan windows and is turned off after a registered tag is read or the 10-second scan window ends.
- Only normal visits with one registered identity and no identity conflict are uploaded. Timeout or unresolved visits remain in diagnostics and are not written to Sheets.
- The firmware opens a maintenance window at boot and after each event for status, diagnostics, pending uploads, and Web OTA.
- A 130 mm RFID coil has read the implanted 2×12 mm FDX-B tags at approximately 10–13 cm in the installed test environment.
- Visit-detection accuracy, RFID read rate, and battery life depend on cabinet geometry, tag orientation, RF conditions, and power quality and should be verified on the installed hardware.

## Security

Copy each `secrets.example.h` to a local `secrets.h` and keep deployment values out of source control. Device tokens, Wi-Fi credentials, raw chip IDs, and image datasets are not intended to be committed.
