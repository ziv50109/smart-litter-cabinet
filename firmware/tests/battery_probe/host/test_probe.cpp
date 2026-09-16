#include "../probe_logic.h"
#include "../../../main/visit_logic.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <random>

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
static std::string frame(const std::string &chip) {
  std::string body = "F" + chip;
  unsigned char sum = 0; for (char c : body) sum ^= static_cast<unsigned char>(c);
  char suffix[4]; std::snprintf(suffix, sizeof(suffix), "%02X#", sum);
  return "$" + body + suffix;
}
static Probe::Parse feed(Probe::Parser &p, const std::string &data, char (&out)[16]) {
  Probe::Parse result = Probe::Parse::None;
  for (char c : data) { const auto r = p.feed(c, out); if (r != Probe::Parse::None) result = r; }
  return result;
}
static void parserTests() {
  Probe::Parser p; char chip[16] = {};
  const std::string good = frame("000000000000001");
  CHECK(feed(p, good, chip) == Probe::Parse::Valid);
  CHECK(std::string(chip) == "000000000000001");
  std::string bad = good; bad[5] = '9';
  CHECK(feed(p, bad, chip) == Probe::Parse::Invalid);
  CHECK(feed(p, "$F0000#", chip) == Probe::Parse::Invalid);
  CHECK(feed(p, frame("00000000000000X"), chip) == Probe::Parse::Invalid);
  CHECK(feed(p, "$" + std::string(200, 'F') + "#", chip) == Probe::Parse::Invalid);
  CHECK(feed(p, "noise" + good, chip) == Probe::Parse::Valid);
  p.reset(); feed(p, good.substr(0, 9), chip); p.reset();
  CHECK(feed(p, good.substr(9), chip) == Probe::Parse::None); // No stale cross-window packet.
  CHECK(feed(p, "$partial" + good, chip) == Probe::Parse::Valid);
  std::puts("PASS parser: checksum, malformed/oversize, noise, cross-window reset");
}
static void controllerTests() {
  Probe::Controller c; c.mode = Probe::Mode::GatedSleep;
  for (uint32_t t = 0; t < 60000; t += 100) c.sample(t, true, 250);
  CHECK(c.generation == 0 && !c.powerWanted() && c.sleepAllowed());
  c.sample(60000, true, 200); CHECK(!c.scanning);
  c.sample(60100, false, 50); CHECK(!c.scanning);
  c.sample(60200, true, 199); CHECK(c.scanning && c.powerWanted() && !c.sleepAllowed());
  CHECK(c.accept(60201)); CHECK(!c.powerWanted() && c.sleepAllowed());
  CHECK(!c.accept(60202));
  for (uint32_t t = 60300; t < 80000; t += 100) c.sample(t, true, 50);
  CHECK(c.generation == 1); // Holding a tag in place cannot leave the reader on.
  c.sample(80000, true, 250); CHECK(!c.powerWanted());
  c.sample(80100, true, 50); CHECK(c.generation == 2 && c.scanning);
  c.tick(90099); CHECK(c.scanning);
  CHECK(!c.accept(90100)); CHECK(!c.powerWanted() && c.end == Probe::End::Timeout);
  c.sample(90200, false, 0); c.sample(90300, true, 50); CHECK(c.generation == 2);
  c.sample(90400, true, 250); CHECK(c.generation == 3); // Clear-edge recovery after a miss.
  const auto start = c.scanStart;
  for (uint32_t t = 90500; t < 100400; t += 100) c.sample(t, true, t % 200 ? 50 : 250);
  CHECK(c.scanStart == start); // Edges cannot keep extending the window.
  c.tick(100400); CHECK(!c.powerWanted());
  c.mode = Probe::Mode::GatedAwake; CHECK(!c.sleepAllowed());
  c.mode = Probe::Mode::ReferenceOn; CHECK(c.powerWanted() && !c.sleepAllowed());
  c.mode = Probe::Mode::None; CHECK(!c.powerWanted());
  std::puts("PASS controller: idle, 200mm boundary, gating, deadline, bounded edge retry, modes");
}
static void timingTests() {
  Probe::Controller c; c.mode = Probe::Mode::GatedSleep;
  const uint32_t nearWrap = UINT32_MAX - 500;
  c.sample(nearWrap, true, 50);
  c.tick(nearWrap + 9999u); CHECK(c.scanning);
  c.tick(nearWrap + 10000u); CHECK(!c.scanning);
  CHECK(Probe::sleepBudget(30, 0, true) == 68);
  CHECK(Probe::sleepBudget(96, 0, true) == 0);
  CHECK(Probe::sleepBudget(0, 0, false) == 0);
  CHECK(Probe::sleepBudget(3000, 0, true) == 0);
  CHECK(Probe::sleepBudget(UINT32_MAX, 0, true) == 0);
  CHECK(Probe::sleepBudget(10, UINT32_MAX - 9, true) == 78);
  CHECK(Probe::checksum("abc", 3) != Probe::checksum("abd", 3));
  std::puts("PASS timing: unsigned wrap, no sleep over sample deadline, capture integrity hash");
}
static Visit::Engine productionTrace(uint32_t exitAt, uint32_t identifyAt) {
  Visit::Engine v;
  for (uint32_t t = 0; t <= 312000; t += 100) {
    const bool blocked = (t >= 1000 && t < 2000) || (t >= exitAt && t < exitAt + 1000);
    v.sample(t, true, blocked ? 80 : 250);
    if (t == identifyAt) CHECK(v.acceptChip(t, "000000000000001"));
    if (v.phase == Visit::Phase::Complete) break;
  }
  return v;
}
static void productionCharacterization() {
  // Tests intentionally document existing main behavior, not a claimed fix.
  const auto shortVisit = productionTrace(7000, 1100);
  CHECK(shortVisit.reason == Visit::Reason::NoExit);
  CHECK(shortVisit.durationMs() == 300000);
  const auto ordinary = productionTrace(20000, 1100);
  CHECK(ordinary.reason == Visit::Reason::Normal);
  CHECK(ordinary.durationMs() == 19000);
  const auto delayedIdentity = productionTrace(7000, 10000);
  CHECK(delayedIdentity.reason == Visit::Reason::NoExit);
  std::puts("KNOWN MAIN LIMITATION REPRODUCED: crossing at 1s/7s -> 300s timeout");
  std::puts("CONTROL TRACE: crossing at 1s/20s -> 19s normal exit (not all <30s visits fail)");
}
static void randomizedInvariants() {
  std::mt19937 rng(12345);
  Probe::Controller c; c.mode = Probe::Mode::GatedSleep;
  uint32_t now = UINT32_MAX - 10000;
  for (unsigned i = 0; i < 100000; ++i) {
    now += 1 + rng() % 150;
    c.tick(now);
    const unsigned action = rng() % 10;
    if (action < 6) c.sample(now, action != 0, action < 3 ? 100 : 250);
    else if (action == 6) c.accept(now);
    CHECK(c.powerWanted() == c.scanning);
    CHECK(c.sleepAllowed() == !c.scanning);
    if (c.scanning) CHECK(uint32_t(now - c.scanStart) < Probe::SCAN_MS);
  }
  std::puts("PASS randomized: 100000 actions across clock wrap and scan boundaries");
}
int main() {
  parserTests(); controllerTests(); timingTests(); productionCharacterization(); randomizedInvariants();
  std::printf("PASS: %u assertions. No ESP32 build, physical RFID, current, or battery validation implied.\n", checks);
}
