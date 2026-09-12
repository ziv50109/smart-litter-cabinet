#pragma once
#include <stdint.h>
#include <time.h>
#include <mutex>

namespace DebugLog {
constexpr unsigned Capacity = 128;
constexpr unsigned PageSize = 16;
struct Row {
  uint32_t seq = 0, uptime = 0, scan = 0, attempt = 0, duration = 0;
  time_t epoch = 0;
  int distance = -1, http = 0;
  char event[81] = {}, chip[16] = {}, cat[64] = {}, detail[128] = {};
  const char *action = "";
};
struct Page {
  Row rows[PageSize];
  unsigned count = 0;
  uint32_t oldest = 0, latest = 0, overwritten = 0;
};
class Ring {
  Row rows[Capacity];
  uint32_t latest = 0;
  std::mutex mutex;
public:
  void append(Row row) {
    std::lock_guard<std::mutex> lock(mutex);
    row.seq = ++latest;
    rows[(row.seq - 1) % Capacity] = row;
  }
  void read(uint32_t after, Page &out) {
    std::lock_guard<std::mutex> lock(mutex);
    out.count = 0; out.latest = latest;
    out.oldest = latest ? (latest > Capacity ? latest - Capacity + 1 : 1) : 0;
    out.overwritten = latest > Capacity ? latest - Capacity : 0;
    if (!latest || after >= latest) return;
    uint32_t first = after + 1;
    if (first < out.oldest) first = out.oldest;
    for (uint32_t seq = first; seq <= latest && out.count < PageSize; ++seq)
      out.rows[out.count++] = rows[(seq - 1) % Capacity];
  }
};
}
