
# XY-134.2K RFID reader test

This sketch prints raw frames and validates the observed `$F` + 15 decimal digits + two-digit XOR checksum + `#` format. With the 130mm coil, the implanted 2×12mm FDX-B chip was read through the cat's skin at approximately 10–13cm. The manufacturer specifies about 20cm for the same tag and coil in a bare-tag 5V test; these conditions are not equivalent.

See `../../../hardware/README.md` for complete sourcing, power, and wiring information. Copy `secrets.example.h` to the ignored `secrets.h` and put Wi-Fi or real chip values only there. Never publish raw chip identifiers in source, screenshots, or logs.

---

# XY-134.2K RFID 讀卡測試

此程式輸出 raw frame，並驗證 `$F` + 15 位數字 + 2 位 XOR checksum + `#` 的實際格式。130mm 線圈讀取植入貓咪皮下的 2×12mm FDX-B 晶片，實測距離約 10–13cm；廠商在 5V 裸測條件下標示同組合約 20cm，兩者測試條件不同。完整採購、供電與接線資料請見 `../../../hardware/README.md`。先複製 `secrets.example.h` 為已忽略的 `secrets.h`，只在該檔填入 Wi-Fi 與真實晶片資料。公開程式與記錄都不得包含真實晶片號碼。
