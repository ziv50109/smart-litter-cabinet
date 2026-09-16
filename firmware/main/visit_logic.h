#pragma once
#include <stdint.h>
#include <string.h>
#include "app_config.h"

namespace Visit {
enum class Phase { Idle, Candidate, Entry, Exit, Complete, WaitClear };
enum class Reason { None, Normal, NoExit, ExitUnconfirmed };
enum class ScanResult { None, Scanning, Valid, Rejected, Timeout, Cancelled };

struct Engine {
  Phase phase = Phase::Idle;
  Reason reason = Reason::None;
  ScanResult scanResult = ScanResult::None;
  uint32_t started = 0, exitStarted = 0, finished = 0;
  uint32_t lastSample = 0, clearSince = 0, scanStarted = 0;
  uint32_t scanGeneration = 0;
  bool sampled = false, previousValid = false, previousBlocked = false;
  bool exitArmed = false, scanning = false, exitScan = false, conflict = false;
  char chip[16] = {}, scanChip[16] = {};

  bool candidate() const { return phase == Phase::Candidate; }
  bool active() const { return phase == Phase::Entry || phase == Phase::Exit; }
  uint32_t elapsed(uint32_t now) const { return uint32_t(now - started); }
  uint32_t currentClearMs(uint32_t now) const {
    return sampled && previousValid && !previousBlocked && clearSince ? uint32_t(now - clearSince) : 0;
  }
  bool hasFreshClear(uint32_t now) const {
    return sampled && previousValid && !previousBlocked &&
      uint32_t(now - lastSample) <= Config::ACTIVE_RANGING_PERIOD_MS * 3;
  }

  void startScan(uint32_t now, bool leaving) {
    if (scanning) return;
    scanning = true;
    exitScan = leaving;
    scanStarted = now;
    scanResult = ScanResult::Scanning;
    scanChip[0] = 0;
    ++scanGeneration;
  }

  void finish(uint32_t now, Reason why) {
    reason = why;
    finished = now;
    phase = Phase::Complete;
    if (scanning) {
      scanning = false;
      scanResult = ScanResult::Cancelled;
    }
  }

  void tick(uint32_t now) {
    if (scanning && uint32_t(now - scanStarted) >= Config::RFID_TIMEOUT_MS) {
      scanning = false;
      scanResult = ScanResult::Timeout;
      if (phase == Phase::Candidate) phase = Phase::Entry;
    }
    if (phase == Phase::Candidate || phase == Phase::Entry) {
      if (elapsed(now) >= Config::MAX_SESSION_DURATION_MS) finish(now, Reason::NoExit);
    } else if (phase == Phase::Exit && elapsed(now) >= Config::EXIT_FINAL_DEADLINE_MS) {
      finish(now, Reason::ExitUnconfirmed);
    }
  }

  void sample(uint32_t now, bool valid, uint16_t mm) {
    tick(now);
    if (phase == Phase::Complete) {
      sampled = true;
      lastSample = now;
      previousValid = valid;
      previousBlocked = valid && mm < Config::ENTRY_THRESHOLD_MM;
      return;
    }

    const bool blocked = valid && mm < Config::ENTRY_THRESHOLD_MM;
    const uint32_t sampleGap = sampled ? uint32_t(now - lastSample) : 0;
    const bool continuous = sampled && sampleGap <= Config::SAMPLE_GAP_MS;
    bool blockedEdge = false;
    uint32_t clearBeforeBlock = 0;

    if (valid) {
      if (blocked) {
        blockedEdge = continuous && previousValid && !previousBlocked;
        if (blockedEdge && clearSince) clearBeforeBlock = uint32_t(now - clearSince);
        clearSince = 0;
      } else if (!continuous || !previousValid || previousBlocked || !clearSince) {
        // Never count an unsampled interval as evidence that the doorway stayed clear.
        clearSince = now;
      }
    } else {
      clearSince = 0;
    }

    sampled = true;
    lastSample = now;
    previousValid = valid;
    previousBlocked = blocked;

    if (phase == Phase::WaitClear) {
      if (valid && !blocked) phase = Phase::Idle;
      return;
    }

    if (phase == Phase::Idle) {
      if (blocked) {
        started = now;
        exitStarted = 0;
        phase = Phase::Candidate;
        reason = Reason::None;
        exitArmed = false;
        conflict = false;
        chip[0] = 0;
        startScan(now, false);
      }
      return;
    }

    if (phase == Phase::Candidate || phase == Phase::Entry) {
      if (valid && !blocked && currentClearMs(now) >= Config::EXIT_ARM_CLEAR_MS) exitArmed = true;
      if (blockedEdge && clearBeforeBlock >= Config::EXIT_ARM_CLEAR_MS) exitArmed = true;
      if (blockedEdge && exitArmed) {
        phase = Phase::Exit;
        exitStarted = now;
        if (!scanning) startScan(now, true);
        return;
      }
      if (phase == Phase::Entry && blockedEdge && !chip[0] && !scanning) startScan(now, false);
    } else if (phase == Phase::Exit) {
      if (!scanning && valid && !blocked && currentClearMs(now) >= Config::EXIT_CLEAR_CONFIRM_MS)
        finish(now, Reason::Normal);
    }
    tick(now);
  }

  bool acceptChip(uint32_t now, const char *value) {
    tick(now);
    if (!scanning || (phase != Phase::Candidate && !active()) || strlen(value) != 15) return false;
    for (unsigned i = 0; i < 15; ++i) if (value[i] < '0' || value[i] > '9') return false;
    memcpy(scanChip, value, 16);
    if (!chip[0]) memcpy(chip, value, 16);
    else if (strcmp(chip, value) != 0) conflict = true;
    scanning = false;
    scanResult = ScanResult::Valid;
    if (phase == Phase::Candidate) phase = Phase::Entry;
    return true;
  }

  bool rejectChip(uint32_t now, const char *value) {
    tick(now);
    if (!scanning || (phase != Phase::Candidate && !active()) || strlen(value) != 15) return false;
    memcpy(scanChip, value, 16);
    scanning = false;
    scanResult = ScanResult::Rejected;
    if (phase == Phase::Candidate) phase = Phase::Entry;
    return true;
  }

  uint32_t durationMs() const { return exitStarted ? uint32_t(exitStarted - started) : 0; }

  void release() {
    const bool ready = hasFreshClear(finished);
    const uint32_t generation = scanGeneration;
    *this = Engine{};
    scanGeneration = generation;
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
