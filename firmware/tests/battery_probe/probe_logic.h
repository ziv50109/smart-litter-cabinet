#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// This is a hardware probe, NOT a visit/direction classifier.
namespace Probe {
constexpr uint16_t THRESHOLD_MM = 200;
constexpr uint32_t SAMPLE_MS = 100;
constexpr uint32_t SCAN_MS = 10000;
constexpr uint32_t CAPTURE_MS = 60000;
constexpr uint32_t WAIT_MS = 900000;
enum class Mode : uint8_t { None, GatedAwake, GatedSleep, ReferenceOn, IdleSleep };
enum class End : uint8_t { None, Identified, Timeout, Cancelled };
enum class Parse : uint8_t { None, Valid, Invalid };

struct Parser {
  char data[19] = {};
  uint8_t length = 0;
  bool receiving = false;
  void reset() { length = 0; receiving = false; }
  static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  }
  Parse feed(char c, char (&chip)[16]) {
    if (c == '$') { length = 0; receiving = true; return Parse::None; }
    if (!receiving) return Parse::None;
    if (c == '#') {
      receiving = false;
      if (length != 18 || data[0] != 'F') return Parse::Invalid;
      uint8_t sum = static_cast<uint8_t>(data[0]);
      for (uint8_t i = 1; i <= 15; ++i) {
        if (data[i] < '0' || data[i] > '9') return Parse::Invalid;
        sum ^= static_cast<uint8_t>(data[i]);
      }
      const int a = hex(data[16]), b = hex(data[17]);
      if (a < 0 || b < 0 || sum != static_cast<uint8_t>((a << 4) | b)) return Parse::Invalid;
      memcpy(chip, data + 1, 15); chip[15] = 0;
      return Parse::Valid;
    }
    if (length >= 18) { reset(); return Parse::Invalid; }
    data[length++] = c;
    return Parse::None;
  }
};

struct Controller {
  Mode mode = Mode::None;
  bool scanning = false, identified = false;
  int8_t lastBlocked = -1; // Invalid observations do not invent an edge.
  uint32_t scanStart = 0, generation = 0;
  End end = End::None;
  bool powerWanted() const { return mode == Mode::ReferenceOn || scanning; }
  bool sleepAllowed() const {
    return (mode == Mode::GatedSleep || mode == Mode::IdleSleep) && !powerWanted();
  }
  void start(uint32_t now) {
    if (scanning) return; // Movement must not indefinitely extend reader power.
    scanStart = now; ++generation; scanning = true; identified = false; end = End::None;
  }
  void tick(uint32_t now) {
    if (scanning && uint32_t(now - scanStart) >= SCAN_MS) {
      scanning = false; end = End::Timeout;
    }
  }
  void sample(uint32_t now, bool valid, uint16_t mm) {
    tick(now);
    if (!valid) return;
    const int8_t blocked = mm < THRESHOLD_MM ? 1 : 0;
    const bool newBlock = blocked == 1 && lastBlocked != 1;
    const bool cleared = blocked == 0 && lastBlocked == 1;
    lastBlocked = blocked;
    // A missed window may retry on a real clear edge. No continuous retries
    // while the entrance remains blocked, invalid, or clear.
    if (newBlock || (cleared && !identified)) start(now);
  }
  bool accept(uint32_t now) {
    tick(now);
    if (!scanning) return false;
    identified = true; scanning = false; end = End::Identified;
    return true;
  }
  void cancel() { if (scanning) { scanning = false; end = End::Cancelled; } }
};

inline uint32_t sleepBudget(uint32_t now, uint32_t lastSample, bool allowed) {
  const uint32_t elapsed = uint32_t(now - lastSample);
  // Wake slightly before the existing deadline, rather than sleeping 100ms
  // AFTER a blocking measurement and silently slowing down sampling.
  return allowed && elapsed < SAMPLE_MS - 4 ? SAMPLE_MS - elapsed - 2 : 0;
}
inline uint32_t checksum(const void *data, size_t size) {
  const auto *p = static_cast<const uint8_t *>(data);
  uint32_t hash = 2166136261u;
  while (size--) { hash ^= *p++; hash *= 16777619u; }
  return hash;
}
} // namespace Probe
