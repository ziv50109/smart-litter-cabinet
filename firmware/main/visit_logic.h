#pragma once
#include <stdint.h>
#include <string.h>
#include "app_config.h"

// Pure production logic: hardware, storage and transport are adapters.
namespace Visit {
enum class Phase { Idle, Entry, Inside, Exit, Complete, WaitClear };
enum class Reason { None, Normal, NoExit, ExitUnconfirmed };
enum class ScanResult { None, Scanning, Valid, Timeout, Cancelled };
struct Engine {
  Phase phase = Phase::Idle;
  Reason reason = Reason::None;
  ScanResult scanResult = ScanResult::None;
  uint32_t started = 0, exitStarted = 0, finished = 0;
  uint32_t clearStarted = 0, lastSample = 0, scanStarted = 0;
  uint32_t scanGeneration = 0;
  bool clear = false, sampled = false, previousValid = false;
  bool previousBlocked = false, scanning = false, exitScan = false;
  bool conflict = false;
  char chip[16] = {}, scanChip[16] = {};

  bool active() const { return phase == Phase::Entry || phase == Phase::Inside || phase == Phase::Exit; }
  uint32_t elapsed(uint32_t now) const { return uint32_t(now - started); }
  uint32_t clearElapsed(uint32_t now) const {
    return clear && sampled && uint32_t(now - lastSample) <= Config::MAX_SAMPLE_GAP_MS
      ? uint32_t(now - clearStarted) : 0;
  }
  void startScan(uint32_t now, bool leaving) {
    if (scanning) return;
    scanning = true; exitScan = leaving; scanStarted = now;
    scanResult = ScanResult::Scanning; scanChip[0] = 0; ++scanGeneration;
  }
  void finish(uint32_t now, Reason why) {
    reason = why; finished = now; phase = Phase::Complete;
    if (scanning) { scanning = false; scanResult = ScanResult::Cancelled; }
  }
  void tick(uint32_t now, bool checkingSample = false) {
    if (clear && sampled && uint32_t(now - lastSample) > Config::MAX_SAMPLE_GAP_MS) clear = false;
    if (scanning && uint32_t(now - scanStarted) >= Config::RFID_TIMEOUT_MS) {
      scanning = false; scanResult = ScanResult::Timeout;
    }
    if (!active()) return;
    if (phase != Phase::Exit && elapsed(now) >= Config::MAX_SESSION_DURATION_MS)
      finish(now, Reason::NoExit);
    else if (phase == Phase::Exit && elapsed(now) >= Config::EXIT_FINAL_DEADLINE_MS &&
             !(checkingSample && elapsed(now) == Config::EXIT_FINAL_DEADLINE_MS))
      finish(now, Reason::ExitUnconfirmed);
  }
  void sample(uint32_t now, bool valid, uint16_t mm) {
    // Deadlines win over newly observed crossings at/after the deadline.
    tick(now, true);
    if (phase == Phase::Complete) {
      // The deadline closes the old event, but this observation still guards rearming.
      sampled = true; lastSample = now; previousValid = valid;
      previousBlocked = valid && mm < Config::ENTRY_THRESHOLD_MM;
      return;
    }
    const bool blocked = valid && mm < Config::ENTRY_THRESHOLD_MM;
    const bool continuous = sampled && uint32_t(now - lastSample) <= Config::MAX_SAMPLE_GAP_MS;
    const bool edge = valid && previousValid && continuous && blocked != previousBlocked;
    if (!valid || blocked || !continuous) clear = false;
    if (valid && !blocked && !clear) { clear = true; clearStarted = now; }
    sampled = true; lastSample = now; previousValid = valid; previousBlocked = blocked;
    if (phase == Phase::WaitClear) { if (valid && !blocked) phase = Phase::Idle; return; }
    if (phase == Phase::Idle) {
      if (blocked) {
        started = now; phase = Phase::Entry; reason = Reason::None;
        conflict = false; chip[0] = 0; startScan(now, false);
      }
      return;
    }
    if (phase == Phase::Entry) {
      if (edge && !chip[0] && !scanning) startScan(now, false);
      if (valid && !blocked && clearElapsed(now) >= Config::CLEAR_INTERVAL_MS) phase = Phase::Inside;
    } else if (phase == Phase::Inside && blocked) {
      phase = Phase::Exit; exitStarted = now;
      // A full clear interval means an entry scan has already expired.
      startScan(now, true);
    } else if (phase == Phase::Exit && valid && !blocked &&
               clearElapsed(now) >= Config::CLEAR_INTERVAL_MS && !scanning) {
      finish(now, Reason::Normal);
    }
    tick(now); // At exactly 100s, a fresh normal confirmation wins; otherwise force closure.
  }
  bool acceptChip(uint32_t now, const char *value) {
    tick(now);
    if (!scanning || !active() || strlen(value) != 15) return false;
    for (unsigned i = 0; i < 15; ++i) if (value[i] < '0' || value[i] > '9') return false;
    memcpy(scanChip, value, 16);
    if (!chip[0]) memcpy(chip, value, 16);
    else if (strcmp(chip, value) != 0) conflict = true;
    scanning = false; scanResult = ScanResult::Valid;
    return true;
  }
  uint32_t durationMs() const {
    return reason == Reason::NoExit ? Config::MAX_SESSION_DURATION_MS : uint32_t(exitStarted - started);
  }
  void release() {
    const bool ready = sampled && uint32_t(finished - lastSample) <= Config::MAX_SAMPLE_GAP_MS &&
                       previousValid && !previousBlocked;
    const uint32_t generation = scanGeneration;
    *this = Engine{}; scanGeneration = generation;
    phase = ready ? Phase::Idle : Phase::WaitClear;
  }
};
inline const char *reasonName(Reason r) {
  switch (r) {
    case Reason::Normal: return "normal_exit";
    case Reason::NoExit: return "no_exit_timeout";
    case Reason::ExitUnconfirmed: return "exit_unconfirmed_timeout";
    default: return "none";
  }
}
}
