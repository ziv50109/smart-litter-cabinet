#pragma once
#include <stdint.h>
#include <string.h>
#include "app_config.h"

// Pure production logic: hardware, storage and transport are adapters.
namespace Visit {
enum class Phase { Idle, Candidate, Entry, Inside, Exit, Complete, WaitClear };
enum class Reason { None, Normal, NoExit, ExitUnconfirmed };
enum class ScanResult { None, Scanning, Valid, Rejected, Timeout, Cancelled };
struct Engine {
  Phase phase = Phase::Idle;
  Reason reason = Reason::None;
  ScanResult scanResult = ScanResult::None;
  uint32_t started = 0, exitStarted = 0, finished = 0;
  uint32_t clearVerifiedMs = 0, lastSample = 0, scanStarted = 0;
  uint32_t scanGeneration = 0;
  bool clear = false, sampled = false, previousValid = false;
  bool previousBlocked = false, scanning = false, exitScan = false;
  bool conflict = false;
  char chip[16] = {}, scanChip[16] = {};

  bool active() const { return phase == Phase::Entry || phase == Phase::Inside || phase == Phase::Exit; }
  bool candidate() const { return phase == Phase::Candidate; }
  bool hasFreshClear(uint32_t now) const {
    return sampled && uint32_t(now - lastSample) <= Config::ACTIVE_RANGING_PERIOD_MS * 2 &&
           previousValid && !previousBlocked;
  }
  uint32_t elapsed(uint32_t now) const { return uint32_t(now - started); }
  uint32_t clearElapsed(uint32_t now) const {
    (void)now;
    return clear ? clearVerifiedMs : 0;
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
    if (scanning && uint32_t(now - scanStarted) >= Config::RFID_TIMEOUT_MS) {
      scanning = false; scanResult = ScanResult::Timeout;
      if (phase == Phase::Candidate)
        phase = Phase::WaitClear;
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
    const bool edge = valid && previousValid && blocked != previousBlocked;
    if (!valid || blocked) {
      clear = false;
      clearVerifiedMs = 0;
    } else if (!clear) {
      clear = true;
      clearVerifiedMs = 0;
    } else {
      const uint32_t interval = uint32_t(now - lastSample);
      const uint32_t credit = interval < Config::ACTIVE_RANGING_PERIOD_MS
        ? interval : Config::ACTIVE_RANGING_PERIOD_MS;
      clearVerifiedMs = UINT32_MAX - clearVerifiedMs < credit
        ? UINT32_MAX : clearVerifiedMs + credit;
    }
    sampled = true; lastSample = now; previousValid = valid; previousBlocked = blocked;
    if (phase == Phase::WaitClear) { if (valid && !blocked) phase = Phase::Idle; return; }
    if (phase == Phase::Idle) {
      if (blocked) {
        started = now; phase = Phase::Candidate; reason = Reason::None;
        conflict = false; chip[0] = 0; startScan(now, false);
      }
      return;
    }
    if (phase == Phase::Candidate) return;
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
    if (!scanning || (!candidate() && !active()) || strlen(value) != 15) return false;
    for (unsigned i = 0; i < 15; ++i) if (value[i] < '0' || value[i] > '9') return false;
    memcpy(scanChip, value, 16);
    if (!chip[0]) memcpy(chip, value, 16);
    else if (strcmp(chip, value) != 0) conflict = true;
    scanning = false; scanResult = ScanResult::Valid;
    if (phase == Phase::Candidate) phase = Phase::Entry;
    return true;
  }
  bool rejectChip(uint32_t now, const char *value) {
    tick(now);
    if (!scanning || (!candidate() && !active()) || strlen(value) != 15) return false;
    memcpy(scanChip, value, 16);
    scanning = false; scanResult = ScanResult::Rejected;
    if (phase == Phase::Candidate)
      phase = hasFreshClear(now) ? Phase::Idle : Phase::WaitClear;
    return true;
  }
  uint32_t durationMs() const {
    return reason == Reason::NoExit ? Config::MAX_SESSION_DURATION_MS : uint32_t(exitStarted - started);
  }
  void release() {
    const bool ready = hasFreshClear(finished);
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
