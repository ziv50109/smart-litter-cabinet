#include <cassert>
#include <iostream>
#include "../../main/visit_logic.h"

using namespace Visit;
static const char *ID = "123456789012345";
static void sample(Engine &e, uint32_t t, uint16_t mm) { e.sample(t, true, mm); }
static void invalid(Engine &e, uint32_t t) { e.sample(t, false, 0); }

int main() {
  // Short visit: a sub-10-second round trip must close normally.
  {
    Engine e;
    sample(e, 1000, 100);
    assert(e.candidate() && e.scanning);
    assert(e.acceptChip(1500, ID));
    assert(e.phase == Phase::Entry);
    sample(e, 2000, 240);
    sample(e, 2100, 240);
    sample(e, 2200, 240);
    sample(e, 2300, 240);
    sample(e, 2400, 100);
    assert(e.phase == Phase::Exit);
    assert(e.exitStarted == 2400);
    if (e.scanning) assert(e.acceptChip(3000, ID));
    sample(e, 2500, 240);
    sample(e, 3000, 240);
    sample(e, 3500, 240);
    assert(e.phase == Phase::Complete);
    assert(e.reason == Reason::Normal);
    assert(e.durationMs() == 1400);
  }

  // Exit scan timeout must not inflate measured visit duration.
  {
    Engine e;
    sample(e, 1000, 100);
    assert(e.acceptChip(1100, ID));
    sample(e, 2000, 240);
    sample(e, 2100, 240);
    sample(e, 2200, 240);
    sample(e, 2300, 240);
    sample(e, 2400, 100);
    assert(e.phase == Phase::Exit);
    for (uint32_t t = 2500; t <= 11500; t += 1000) sample(e, t, 240);
    e.tick(12401);
    assert(!e.scanning);
    sample(e, 12500, 240);
    assert(e.phase == Phase::Complete);
    assert(e.reason == Reason::Normal);
    assert(e.durationMs() == 1400);
  }

  // Staying blocked must not fabricate an exit.
  {
    Engine e;
    sample(e, 1000, 100);
    assert(e.acceptChip(1100, ID));
    sample(e, 1500, 150);
    sample(e, 2000, 180);
    assert(e.phase == Phase::Entry);
    assert(e.exitStarted == 0);
  }

  // A long unsampled gap is not evidence that the doorway stayed clear.
  {
    Engine e;
    sample(e, 1000, 100);
    assert(e.acceptChip(1100, ID));
    sample(e, 2000, 240);
    sample(e, 2100, 240);
    sample(e, 4000, 100);  // 1.9s gap > SAMPLE_GAP_MS
    assert(e.phase == Phase::Entry);
    assert(e.exitStarted == 0);
  }

  // Invalid ranging breaks clear continuity.
  {
    Engine e;
    sample(e, 1000, 100);
    assert(e.acceptChip(1100, ID));
    sample(e, 2000, 240);
    sample(e, 2100, 240);
    invalid(e, 2200);
    sample(e, 2300, 100);
    assert(e.phase == Phase::Entry);
    assert(e.exitStarted == 0);
  }

  std::cout << "main visit logic: PASS\n";
}
