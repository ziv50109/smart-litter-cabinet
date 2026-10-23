#include <cassert>
#include <iostream>
#include "../../main/runtime_policy.h"
using namespace Runtime;

int main() {
  Window window;
  Backoff backoff;
  constexpr uint32_t pollMs = 50;
  assert(networkWaitMs(false, false, window, backoff, 0, pollMs) == UINT32_MAX);
  assert(networkWaitMs(true, false, window, backoff, 0, pollMs) == pollMs);
  assert(networkWaitMs(false, true, window, backoff, 0, pollMs) == 0);

  // A request wakes an otherwise idle task; only its unexpired retry can delay connection.
  window.open(100, 1000);
  assert(networkWaitMs(false, false, window, backoff, 100, pollMs) == 0);
  backoff.fail(100, 5000, 60000);
  assert(networkWaitMs(false, false, window, backoff, 200, pollMs) == 900);
  assert(networkWaitMs(false, true, window, backoff, 200, pollMs) == 4900);
  assert(networkWaitMs(true, true, window, backoff, 200, pollMs) == pollMs);
  assert(networkWaitMs(false, false, window, backoff, 1100, pollMs) == UINT32_MAX);
  assert(networkWaitMs(false, true, window, backoff, 5099, pollMs) == 1);
  assert(networkWaitMs(false, true, window, backoff, 5100, pollMs) == 0);
  window.open(5100, 60000);
  backoff.fail(5100, 5000, 60000);
  assert(networkWaitMs(false, false, window, backoff, 5101, pollMs) == 9999);

  // Monotonic network deadlines survive both millis rollover and an entire rollover asleep.
  const uint64_t beforeWrap = uint64_t(UINT32_MAX) - 10;
  window.open(beforeWrap, 20);
  backoff.reset(); backoff.fail(beforeWrap, 30, 80);
  assert(window.remaining(beforeWrap + 19) == 1);
  assert(networkWaitMs(false, false, window, backoff, beforeWrap + 19, pollMs) == 1);
  assert(networkWaitMs(false, false, window, backoff, beforeWrap + 20, pollMs) == UINT32_MAX);
  assert(networkWaitMs(false, true, window, backoff, beforeWrap + 20, pollMs) == 10);
  const uint64_t longIdle = beforeWrap + uint64_t(UINT32_MAX) + 2;
  assert(!window.active(longIdle) && backoff.ready(longIdle));
  assert(networkWaitMs(false, false, window, backoff, longIdle, pollMs) == UINT32_MAX);
  window.open(longIdle, 1000);
  assert(networkWaitMs(false, false, window, backoff, longIdle, pollMs) == 0);

  OnTime on;
  assert(on.elapsedUs(100) == 0);
  on.set(false, 100);
  on.set(true, 200); on.set(true, 300);
  assert(on.elapsedUs(450) == 250); // An in-progress scan/connection is visible.
  on.set(false, 500); on.set(false, 600);
  assert(on.elapsedUs(700) == 300);
  on.set(true, 800); on.set(false, 1000);
  assert(on.elapsedUs(2000) == 500);
  const uint64_t longOn = (uint64_t(UINT32_MAX) + 100) * 1000;
  on.set(true, 3000);
  assert(on.elapsedUs(3000 + longOn) == 500 + longOn);
  on.set(false, 3000 + longOn);
  assert(on.elapsedUs(4000 + longOn) == 500 + longOn);

  BootPowerStats stats;
  stats.recordSleep(true, 180000);
  stats.recordSleep(false, 2000); // Failed attempts must not count as time asleep.
  stats.recordSleep(true, longOn);
  assert(stats.manualSleepCalls == 2 && stats.manualSleepErrors == 1);
  assert(stats.manualSleepUs == 180000 + longOn);
  assert(stats.rfidScans == 0 && stats.rfid.elapsedUs(longOn) == 0);
  assert(stats.wifi.elapsedUs(longOn) == 0);
  std::cout << "power accounting and network deadlines: PASS\n";
}
