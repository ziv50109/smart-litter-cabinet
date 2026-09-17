#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Trace {
struct Sample { uint32_t ms; uint16_t mm; uint8_t flags; };
struct Event { uint32_t ms; uint8_t code; uint8_t value; uint16_t aux; };
template <size_t SampleCapacity, size_t EventCapacity> struct Buffer {
  static_assert(SampleCapacity > 0 && SampleCapacity <= UINT16_MAX, "invalid sample capacity");
  static_assert(EventCapacity > 1 && EventCapacity <= UINT16_MAX, "invalid event capacity");
  Sample samples[SampleCapacity];
  Event events[EventCapacity];
  uint16_t head = 0, count = 0, eventCount = 0;
  uint32_t total = 0, eventsDropped = 0;
  uint8_t lastDistanceState = 255;
  void reset() { head = count = eventCount = 0; total = eventsDropped = 0; lastDistanceState = 255; }
  void add(Sample sample) {
    ++total;
    if (count < SampleCapacity) samples[(head + count++) % SampleCapacity] = sample;
    else { samples[head] = sample; head = (head + 1) % SampleCapacity; }
  }
  const Sample &at(size_t i) const { return samples[(head + i) % SampleCapacity]; }
  uint32_t overwritten() const { return total - count; }
  void event(Event value, bool terminal = false) {
    // Reserve one slot for the reason that closes the session.
    if (eventCount < EventCapacity - (terminal ? 0 : 1)) events[eventCount++] = value;
    else ++eventsDropped;
  }
};
}  // namespace Trace
