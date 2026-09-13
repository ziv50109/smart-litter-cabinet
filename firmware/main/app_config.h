#pragma once

#include <stdint.h>

namespace Config {
constexpr uint8_t RFID_ENABLE_PIN = 1;  // D0 / GPIO1, HIGH = XY-134.2K ON
constexpr uint8_t TOF_INT_PIN = 2;      // D1 / GPIO2, reserved for later INT wake tuning
constexpr uint8_t TOF_SDA_PIN = 5;      // D4 / GPIO5
constexpr uint8_t TOF_SCL_PIN = 6;      // D5 / GPIO6
constexpr uint8_t RFID_TX_PIN = 43;     // D6 / GPIO43 -> XY RXD
constexpr uint8_t RFID_RX_PIN = 44;     // D7 / GPIO44 <- XY TXD

constexpr uint16_t ENTRY_THRESHOLD_MM = 200;
constexpr uint16_t EXIT_THRESHOLD_MM = 200;
constexpr uint32_t CLEAR_INTERVAL_MS = 10000;
constexpr uint32_t CLEAR_SAMPLE_GAP_MS = 1000;
constexpr uint32_t IDLE_RANGING_PERIOD_MS = 100;
constexpr uint32_t ACTIVE_RANGING_PERIOD_MS = 100;
constexpr uint32_t TOF_TIMING_BUDGET_US = 20000;
constexpr uint32_t RFID_TIMEOUT_MS = 10000;
constexpr uint32_t WIFI_TIMEOUT_MS = 15000;
constexpr uint32_t NTP_TIMEOUT_MS = 10000;
constexpr uint32_t TOF_INVALID_RESTART_MS = 5000;
constexpr uint32_t TOF_REINIT_BACKOFF_MS = 5000;
constexpr uint32_t SESSION_CHECKPOINT_MS = 30000;
constexpr uint32_t MAX_SESSION_DURATION_MS = 300000;
constexpr uint32_t EXIT_FINAL_DEADLINE_MS = 310000;
constexpr uint8_t MAX_PENDING_RECORDS = 8;
}  // namespace Config
