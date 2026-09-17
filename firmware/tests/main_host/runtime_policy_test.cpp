#include <cassert>
#include <iostream>
#include "../../main/runtime_policy.h"
#include "../../main/trace_buffer.h"
#include "../../main/visit_logic.h"
using namespace Runtime;

int main() {
  uint32_t number = 77;
  for (const char *bad : {"", "-1", "+0", "0x1", "1abc", " 1", "1 ", "4294967296"}) {
    assert(!decimal(bad, number)); assert(number == 77);
  }
  assert(!decimal(nullptr, number));
  assert(decimal("0", number) && number == 0);
  assert(decimal("4294967295", number) && number == UINT32_MAX);
  assert(safeSessionId("0E534398-03625FF7-8FEE68"));
  for (const char *bad : {"", "../secrets", "x.csv", "a\r\nHeader", "<script>", "a\"", "a/b"}) assert(!safeSessionId(bad));

  assert(queuedTimestampState("2026-09-17T13:00:00Z", "boot-a", false) == QueuedTimestampState::Ready);
  assert(queuedTimestampState("@boot-a:12345", "boot-a", false) == QueuedTimestampState::WaitForClock);
  assert(queuedTimestampState("@boot-a:12345", "boot-a", true) == QueuedTimestampState::Ready);
  for (const char *bad : {"", "@", "@boot-a:", "@boot-a:12x", "@boot-b:12345"}) {
    assert(queuedTimestampState(bad, "boot-a", true) == QueuedTimestampState::Unrecoverable);
  }
  assert(!queueHeadUnrecoverable("@boot-a:100", "@boot-a:200", "boot-a", false));
  assert(queueHeadUnrecoverable("@boot-old:100", "@boot-old:200", "boot-a", true));
  assert(queueHeadUnrecoverable("", "2026-09-17T13:00:00Z", "boot-a", true));

  Window w;
  assert(!w.active(0));
  w.open(UINT32_MAX - 10, 20);
  assert(w.active(0)); assert(w.active(8)); assert(!w.active(9));
  w.close(); assert(!w.active(UINT32_MAX));
  Backoff b;
  assert(b.ready(0));
  b.fail(UINT32_MAX - 10, 20, 80);
  assert(!b.ready(8)); assert(b.ready(9));
  b.fail(10, 20, 80); assert(b.delayMs == 40);
  b.fail(50, 20, 80); assert(b.delayMs == 80);
  b.fail(130, 20, 80); assert(b.delayMs == 80);
  b.reset(); assert(b.ready(130));

  OtaTransfer t;
  assert(!t.begin(1, false, true, 100, 1000));
  assert(!t.add(2, 100, 200)); assert(!t.ready(2, 200));
  t.reset(); assert(!t.begin(1, true, false, 100, 1000));
  t.reset(); assert(!t.begin(1, true, true, 0, 1000));
  t.reset(); assert(!t.begin(1, true, true, 1001, 1000));
  t.reset(); assert(t.begin(1, true, true, 100, 1000));
  assert(t.add(2, 30, 200)); assert(t.add(3, 70, 200));
  assert(!t.ready(3, 200)); assert(t.end(4, 200)); assert(t.ready(4, 200));
  assert(!t.begin(5, true, true, 100, 1000)); // Second multipart file invalidates whole request.
  assert(!t.ready(5, 200));
  t.reset(); assert(t.begin(1, true, true, 100, 1000));
  assert(t.add(2, 99, 200)); assert(!t.end(3, 200)); assert(!t.ready(3, 200));
  t.reset(); assert(t.begin(1, true, true, 100, 1000));
  assert(!t.add(2, 101, 200)); assert(t.received == 0);
  t.reset(); assert(t.begin(UINT32_MAX - 10, true, true, 100, 1000));
  assert(t.add(0, 100, 20)); assert(t.end(8, 20)); assert(!t.ready(9, 20));
  t.reset(); assert(!t.add(1, 100, 200)); // No authorization leaks between requests.

  assert(canPauseForOta(true, true, false, 5000, 4900, 1000, 600, 1000));
  assert(!canPauseForOta(false, true, false, 5000, 4900, 1000, 600, 1000));
  assert(!canPauseForOta(true, false, false, 5000, 4900, 1000, 600, 1000));
  assert(!canPauseForOta(true, true, true, 5000, 4900, 1000, 600, 1000));
  assert(!canPauseForOta(true, true, false, 5000, 4300, 1000, 600, 1000));
  assert(!canPauseForOta(true, true, false, 5000, 4900, 999, 600, 1000));

  Trace::Buffer<3, 3> small;
  for (unsigned i = 0; i < 5; ++i) small.add({i, uint16_t(i), 1});
  assert(small.count == 3 && small.overwritten() == 2);
  assert(small.at(0).ms == 2 && small.at(2).ms == 4);
  small.event({1, 1, 0, 0}); small.event({2, 2, 0, 0}); small.event({3, 3, 0, 0});
  small.event({4, 10, 2, 0}, true);
  assert(small.eventCount == 3 && small.eventsDropped == 1 && small.events[2].code == 10);
  small.reset(); assert(small.count == 0 && small.total == 0 && small.eventsDropped == 0);

  // A complete configured session fits, including the previously lost entry samples.
  Trace::Buffer<Config::TRACE_SAMPLE_CAP, Config::TRACE_EVENT_CAP> full;
  for (uint32_t ms = 0; ms <= Config::EXIT_FINAL_DEADLINE_MS; ms += Config::ACTIVE_RANGING_PERIOD_MS)
    full.add({ms, uint16_t(ms < 1000 ? 50 : 239), 1});
  assert(full.overwritten() == 0 && full.at(0).mm == 50);
  Visit::Engine e;
  e.sample(1000, true, 50); assert(e.acceptChip(1505, "123456789012345"));
  for (uint32_t now = 1600; now <= 301000; now += 100) e.sample(now, true, 239);
  assert(e.phase == Visit::Phase::Complete && e.reason == Visit::Reason::NoExit && e.durationMs() == 0);
  std::cout << "runtime policies and trace: PASS\n";
}
