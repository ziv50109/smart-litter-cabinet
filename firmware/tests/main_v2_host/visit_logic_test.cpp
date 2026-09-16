#include <cassert>
#include <iostream>
#include "../../main_v2/visit_logic.h"

using namespace Visit;
static const char *ID = "953010008115531";
static void sample(Engine &e, uint32_t t, uint16_t mm) { e.sample(t, true, mm); }

int main() {
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
    sample(e, 2500, 240);
    e.tick(12401);
    assert(!e.scanning);
    sample(e, 12500, 240);
    assert(e.phase == Phase::Complete);
    assert(e.reason == Reason::Normal);
    assert(e.durationMs() == 1400);
  }
  {
    Engine e;
    sample(e, 1000, 100);
    assert(e.acceptChip(1100, ID));
    sample(e, 1500, 150);
    sample(e, 2000, 180);
    assert(e.phase == Phase::Entry);
    assert(e.exitStarted == 0);
  }
  std::cout << "main_v2 visit logic: PASS\n";
}
