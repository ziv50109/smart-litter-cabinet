#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Hardware-independent policy shared by the firmware and host regression tests.
namespace Runtime {
inline bool decimal(const char *text, uint32_t &out) {
  if (!text || !*text) return false;
  uint32_t n = 0;
  for (const char *p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    const uint32_t digit = uint32_t(*p - '0');
    if (n > (UINT32_MAX - digit) / 10) return false;
    n = n * 10 + digit;
  }
  out = n;
  return true;
}
inline bool safeSessionId(const char *text) {
  if (!text) return false;
  const size_t n = strlen(text);
  if (!n || n > 80) return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = text[i];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
          (c >= 'a' && c <= 'z') || c == '-')) return false;
  }
  return true;
}

enum class QueuedTimestampState : uint8_t { Ready, WaitForClock, Unrecoverable };
inline QueuedTimestampState queuedTimestampState(const char *stamp, const char *bootId, bool clockValid) {
  if (!stamp || !*stamp) return QueuedTimestampState::Unrecoverable;
  if (*stamp != '@') return QueuedTimestampState::Ready;
  const char *colon = strchr(stamp + 1, ':');
  if (!colon || colon == stamp + 1 || !bootId || !*bootId) return QueuedTimestampState::Unrecoverable;
  const size_t tokenLen = size_t(colon - (stamp + 1));
  if (strlen(bootId) != tokenLen || strncmp(stamp + 1, bootId, tokenLen) != 0) {
    return QueuedTimestampState::Unrecoverable;
  }
  const char *digits = colon + 1;
  if (!*digits) return QueuedTimestampState::Unrecoverable;
  for (const char *p = digits; *p; ++p) {
    if (*p < '0' || *p > '9') return QueuedTimestampState::Unrecoverable;
  }
  return clockValid ? QueuedTimestampState::Ready : QueuedTimestampState::WaitForClock;
}
inline bool queueHeadUnrecoverable(const char *enterTime, const char *exitTime, const char *bootId, bool clockValid) {
  return queuedTimestampState(enterTime, bootId, clockValid) == QueuedTimestampState::Unrecoverable ||
         queuedTimestampState(exitTime, bootId, clockValid) == QueuedTimestampState::Unrecoverable;
}

struct Window {
  uint32_t started = 0, duration = 0;
  void open(uint32_t now, uint32_t ms) { started = now; duration = ms; }
  void close() { duration = 0; }
  bool active(uint32_t now) const { return duration && uint32_t(now - started) < duration; }
};
struct Backoff {
  uint32_t failedAt = 0, delayMs = 0;
  bool ready(uint32_t now) const { return !delayMs || uint32_t(now - failedAt) >= delayMs; }
  void reset() { delayMs = 0; }
  void fail(uint32_t now, uint32_t initial, uint32_t maximum) {
    failedAt = now;
    delayMs = !delayMs ? initial : (delayMs >= maximum / 2 ? maximum : delayMs * 2);
    if (delayMs > maximum) delayMs = maximum;
  }
};

// One instance belongs to one HTTP upload transaction, never to a logged-in user.
struct OtaTransfer {
  bool active = false, failed = false, ended = false;
  uint32_t started = 0, expected = 0, received = 0;
  void reset() { *this = OtaTransfer{}; }
  bool begin(uint32_t now, bool authorized, bool safeToPause, uint32_t size, uint32_t capacity) {
    // A second multipart file must poison, not replace, the first transfer.
    if (active || failed || !authorized || !safeToPause || !size || size > capacity) {
      failed = true;
      return false;
    }
    active = true; started = now; expected = size;
    return true;
  }
  bool expired(uint32_t now, uint32_t limit) const {
    return active && uint32_t(now - started) >= limit;
  }
  bool add(uint32_t now, size_t bytes, uint32_t limit) {
    if (!active || failed || ended || expired(now, limit) || bytes > expected - received) {
      failed = true;
      return false;
    }
    received += uint32_t(bytes);
    return true;
  }
  bool end(uint32_t now, uint32_t limit) {
    if (!active || failed || ended || expired(now, limit) || received != expected) {
      failed = true;
      return false;
    }
    ended = true;
    return true;
  }
  bool ready(uint32_t now, uint32_t limit) const {
    return active && !failed && ended && received == expected && !expired(now, limit);
  }
};
inline bool canPauseForOta(bool idle, bool valid, bool blocked, uint32_t now,
                           uint32_t lastSample, uint32_t clearMs,
                           uint32_t freshness, uint32_t clearRequired) {
  return idle && valid && !blocked && uint32_t(now - lastSample) <= freshness && clearMs >= clearRequired;
}
}  // namespace Runtime
