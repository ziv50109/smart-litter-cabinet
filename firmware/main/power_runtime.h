#pragma once

void configurePower() {
  if (!LITTER_CONNECTED_STANDBY) return;
  if (!adminConfigured()) { powerReason = "admin_credentials_missing"; return; }
#if CONFIG_PM_ENABLE && CONFIG_FREERTOS_USE_TICKLESS_IDLE
  esp_err_t a = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "rfid-rx", &rfidSleepLock);
  esp_err_t b = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "rfid-clock", &rfidClockLock);
  if (a != ESP_OK || b != ESP_OK) {
    if (rfidSleepLock) { esp_pm_lock_delete(rfidSleepLock); rfidSleepLock = nullptr; }
    if (rfidClockLock) { esp_pm_lock_delete(rfidClockLock); rfidClockLock = nullptr; }
    powerReason = "pm_lock_init_failed"; return;
  }
  // Use the SDK's configured ceiling, not the frequency at a transient DFS state.
  // This also avoids depending on Arduino's CPU HAL linkage in the IDF component build.
  static_assert(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ >= 80, "PM requires an S3 CPU ceiling of at least 80 MHz");
  esp_pm_config_t pm = {};
  pm.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  pm.min_freq_mhz = 80;
  pm.light_sleep_enable = true;
  const esp_err_t result = esp_pm_configure(&pm);
  if (result != ESP_OK) {
    esp_pm_lock_delete(rfidSleepLock); esp_pm_lock_delete(rfidClockLock);
    rfidSleepLock = rfidClockLock = nullptr;
    powerReason = "pm_configuration_failed"; return;
  }
  automaticPm.store(true); connectedStandby.store(true);
  powerReason = "automatic_configured_not_measured";
#else
  // A sketch #define cannot enable capabilities missing from precompiled SDK libraries.
  powerReason = "sdk_pm_or_tickless_unavailable";
#endif
  logLive("power", powerReason.load());
}
void maybeSleep() {
  if (automaticPm.load() || rfidPower || netMode.load() != NetMode::Off) return;
  ScopedLock power(powerGate, 0);
  if (!power.held || netMode.load() != NetMode::Off) return;
  const uint32_t period = (visit.phase == Visit::Phase::Idle || visit.phase == Visit::Phase::WaitClear)
    ? Config::IDLE_RANGING_PERIOD_MS : Config::ACTIVE_RANGING_PERIOD_MS;
  const uint32_t elapsed = uint32_t(millis() - lastRangeMs);
  if (elapsed + 5 >= period) return;
  const uint32_t ms = period - elapsed - 2;
  const int64_t before = esp_timer_get_time();
  const esp_err_t timer = esp_sleep_enable_timer_wakeup(uint64_t(ms) * 1000ULL);
  const esp_err_t result = timer == ESP_OK ? esp_light_sleep_start() : timer;
  if (metrics.open) {
    if (result == ESP_OK) { ++metrics.sleepCalls; metrics.sleepMs += uint32_t((esp_timer_get_time() - before) / 1000); }
    else ++metrics.sleepErrors;
  }
}
