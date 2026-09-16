#pragma once
#include <stdint.h>

namespace Config {
constexpr uint8_t RFID_ENABLE_PIN = 1;
constexpr uint8_t TOF_INT_PIN = 2;
constexpr uint8_t TOF_SDA_PIN = 5;
constexpr uint8_t TOF_SCL_PIN = 6;
constexpr uint8_t RFID_RX_PIN = 44;

constexpr uint16_t ENTRY_THRESHOLD_MM = 200;
constexpr uint32_t IDLE_RANGING_PERIOD_MS = 200;
constexpr uint32_t ACTIVE_RANGING_PERIOD_MS = 100;
constexpr uint32_t SAMPLE_GAP_MS = 1000;
constexpr uint32_t EXIT_ARM_CLEAR_MS = 250;
constexpr uint32_t EXIT_CLEAR_CONFIRM_MS = 1000;
constexpr uint32_t TOF_TIMING_BUDGET_US = 20000;
constexpr uint32_t RFID_TIMEOUT_MS = 10000;
constexpr uint32_t MAX_SESSION_DURATION_MS = 300000;
constexpr uint32_t EXIT_FINAL_DEADLINE_MS = 310000;

constexpr uint32_t WIFI_TIMEOUT_MS = 8000;
constexpr uint32_t NTP_TIMEOUT_MS = 8000;
constexpr uint32_t BOOT_MAINTENANCE_MS = 120000;
constexpr uint32_t POST_EVENT_MAINTENANCE_MS = 60000;
constexpr uint32_t RETRY_UPLOAD_AFTER_MS = 60000;
constexpr uint8_t MAX_PENDING_RECORDS = 8;
constexpr uint8_t TRACE_SLOTS = 8;
constexpr uint32_t DIAG_ROTATE_BYTES = 65536;
}
