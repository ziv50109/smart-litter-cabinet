#pragma once
#include <stdint.h>

namespace Config {
constexpr uint8_t RFID_ENABLE_PIN = 1;  // D0 / GPIO1, HIGH = XY-134.2K ON
constexpr uint8_t TOF_INT_PIN = 2;      // D1 / GPIO2, reserved for future INT wake tuning
constexpr uint8_t TOF_SDA_PIN = 5;      // D4 / GPIO5
constexpr uint8_t TOF_SCL_PIN = 6;      // D5 / GPIO6
constexpr uint8_t RFID_RX_PIN = 44;     // D7 / GPIO44 <- XY TXD

constexpr uint16_t ENTRY_THRESHOLD_MM = 200;
constexpr uint32_t IDLE_RANGING_PERIOD_MS = 200;
constexpr uint32_t ACTIVE_RANGING_PERIOD_MS = 100;
constexpr uint32_t SAMPLE_GAP_MS = 1000;
constexpr uint32_t EXIT_ARM_CLEAR_MS = 250;
constexpr uint32_t EXIT_CLEAR_CONFIRM_MS = 1000;
constexpr uint32_t TOF_TIMING_BUDGET_US = 20000;
constexpr uint32_t TOF_INVALID_RESTART_MS = 5000;
constexpr uint32_t TOF_REINIT_BACKOFF_MS = 5000;
constexpr uint32_t RFID_TIMEOUT_MS = 10000;
constexpr uint32_t MAX_SESSION_DURATION_MS = 300000;
constexpr uint32_t EXIT_FINAL_DEADLINE_MS = 310000;

constexpr uint32_t WIFI_TIMEOUT_MS = 8000;
constexpr uint32_t NTP_TIMEOUT_MS = 8000;
constexpr uint32_t BOOT_MAINTENANCE_MS = 120000;
constexpr uint32_t POST_EVENT_MAINTENANCE_MS = 60000;
constexpr uint8_t MAX_PENDING_RECORDS = 8;
constexpr uint8_t TRACE_SLOTS = 8;
constexpr uint32_t DIAG_ROTATE_BYTES = 65536;

// Management and scheduling; detection settings above retain their existing semantics.
constexpr uint32_t NETWORK_POLL_MS = 50;
constexpr uint32_t NETWORK_RETRY_INITIAL_MS = 5000;
constexpr uint32_t NETWORK_RETRY_MAX_MS = 60000;
constexpr uint32_t UPLOAD_RETRY_MS = 30000;
constexpr uint32_t ADMIN_NONCE_LIFETIME_MS = 300000;
constexpr uint32_t ADMIN_RETRY_COOLDOWN_MS = 5000;
constexpr uint8_t ADMIN_FAILURE_LIMIT = 5;
constexpr uint32_t OTA_DEADLINE_MS = 120000;
constexpr uint32_t OTA_FRESH_SAMPLE_MS = IDLE_RANGING_PERIOD_MS * 3;
constexpr uint32_t OTA_CLEAR_MS = EXIT_CLEAR_CONFIRM_MS;
constexpr uint32_t TRACE_SAMPLE_CAP = EXIT_FINAL_DEADLINE_MS / ACTIVE_RANGING_PERIOD_MS + 16;
constexpr uint16_t TRACE_EVENT_CAP = 256;
constexpr uint8_t DIAG_SNAPSHOT_SLOTS = 2;
constexpr uint32_t MAX_DOWNLOAD_BYTES = 160 * 1024;
static_assert(TRACE_SAMPLE_CAP <= 4096, "review RAM budget before increasing session duration");
}  // namespace Config
