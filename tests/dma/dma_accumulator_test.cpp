#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <random>
#include <source_location>
#include <span>
#include <stdexcept>
#include <vector>

constexpr int HOR_RES = 466;
constexpr int SLINT_CHUNK_ACCUMULATORS = 3;
constexpr int capacity = HOR_RES * 14;
int slint_chunk_lines = 14;
using PixelType = uint16_t;
using SemaphoreHandle_t = bool;
SemaphoreHandle_t trans_sem = true;
constexpr int pdTRUE = 1, ESP_OK = 0;
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGW(...) throw std::runtime_error("Unexpected production warning")
std::array<std::array<uint16_t, capacity>, 3> storage;
uint16_t *slint_chunk_buffer[] = {storage[0].data(), storage[1].data(), storage[2].data()};
uint64_t g_drawcall_us = 0;
uint64_t esp_timer_get_time() {
  static uint64_t time;
  return ++time;
}
void require(bool condition, std::source_location where = std::source_location::current()) {
  if (!condition) {
    std::fprintf(stderr, "DMA invariant failed at %s:%u\n", where.file_name(), where.line());
    std::fflush(stderr);
    throw std::runtime_error("DMA invariant failed");
  }
}
void byte_swap_buffer(uint16_t *p, size_t count) {
  for (size_t i = 0; i < count; ++i)
    p[i] = uint16_t((p[i] << 8) | (p[i] >> 8));
}
struct Segment {
  int y, x0, x1;
};
struct Transfer {
  size_t x0, y0, x1, y1;
  uint16_t *data;
};
std::optional<Transfer> pending;
int tokens, salt;
bool automatic_completion;
std::vector<uint16_t> panel;
std::vector<int> writes;
uint16_t pixel(int x, int y) {
  return uint16_t(x * 73 + y * 191 + salt * 17);
}
void complete() {
  require(pending.has_value());
  auto t = *pending;
  for (size_t y = t.y0; y < t.y1; ++y)
    for (size_t x = t.x0; x < t.x1; ++x) {
      panel[y * HOR_RES + x] = t.data[(y - t.y0) * (t.x1 - t.x0) + x - t.x0];
      ++writes[y * HOR_RES + x];
    }
  pending.reset();
  if (trans_sem)
    ++tokens;
}
int xSemaphoreTake(bool, int) {
  if (!tokens && pending)
    complete();
  require(tokens > 0);
  --tokens;
  return pdTRUE;
}
int timed_draw_bitmap(int, size_t x0, size_t y0, size_t x1, size_t y1, uint16_t *data) {
  require(!pending && tokens == 0);
  require(x0 < x1 && y0 < y1 && x1 <= HOR_RES && y1 <= HOR_RES);
  // The AMOLED backend uses asynchronous DMA and requires even address windows.
  if (trans_sem)
    require((x0 | x1 | y0 | y1) % 2 == 0);
  require((x1 - x0) * (y1 - y0) <= capacity);
  require(std::find(std::begin(slint_chunk_buffer), std::end(slint_chunk_buffer), data) !=
          std::end(slint_chunk_buffer));
  pending = Transfer{x0, y0, x1, y1, data};
  if (!trans_sem)
    complete();
  return ESP_OK;
}
struct Renderer {
  const std::vector<Segment> *segments;
  template <class Pixel, class Callback> int render_by_line(Callback callback) {
    for (auto s : *segments) {
      if (automatic_completion && pending && (s.y % 3 == 0))
        complete();
      callback(s.y, s.x0, s.x1, [&](std::span<Pixel> row) {
        auto address = reinterpret_cast<uintptr_t>(row.data());
        bool within = false;
        for (auto *base : slint_chunk_buffer) {
          auto lo = reinterpret_cast<uintptr_t>(base);
          within |= address >= lo && address + row.size_bytes() <= lo + capacity * sizeof(Pixel);
        }
        require(within && row.size() == size_t(s.x1 - s.x0));
        if (pending) {
          auto lo = reinterpret_cast<uintptr_t>(pending->data);
          auto hi = lo + (pending->x1 - pending->x0) * (pending->y1 - pending->y0) * sizeof(Pixel);
          require(address + row.size_bytes() <= lo || address >= hi);
        }
        for (int x = s.x0; x < s.x1; ++x)
          row[x - s.x0] = pixel(x, s.y);
      });
    }
    return 0;
  }
};
struct Window {
  Renderer m_renderer;
};
void render(const std::vector<Segment> &segments, bool byte_swap) {
  Window window{{&segments}};
  auto *m_window = &window;
  int panel_handle = 0;
  uint64_t t_start = 0, t_render = 0, t_copy = 0, t_wait_transmit = 0, t_prepare = 0;
  uint32_t frame_lines = 0;
#include DMA_SOURCE
}
void check(const std::vector<Segment> &segments, bool swap) {
  panel.assign(HOR_RES * HOR_RES, 0xbeef);
  writes.assign(panel.size(), 0);
  std::vector<uint16_t> expected = panel;
  std::vector<int> expected_writes = writes;
  for (auto s : segments)
    for (int x = s.x0; x < s.x1; ++x) {
      auto p = pixel(x, s.y);
      expected[s.y * HOR_RES + x] = swap ? uint16_t((p << 8) | (p >> 8)) : p;
      ++expected_writes[s.y * HOR_RES + x];
    }
  require(!pending && tokens == 0);
  render(segments, swap);
  require(!pending && tokens == 0);
  require(panel == expected && writes == expected_writes);
}
int main() {
  std::mt19937 rng(20260927);
  int cases = 0;
  for (int scene = 0; scene < 4000; ++scene) {
    std::vector<Segment> segments;
    // PhysicalRegion holds at most three rectangles; scanlines use their union.
    std::vector<bool> dirty(HOR_RES * HOR_RES);
    int count = scene == 1 ? 0 : 1 + rng() % 3;
    for (int i = 0; i < count; ++i) {
      int x0 = scene == 0 ? 0 : 2 * (rng() % (HOR_RES / 2));
      int y0 = scene == 0 ? 0 : 2 * (rng() % (HOR_RES / 2));
      int x1 = scene == 0 ? HOR_RES : x0 + 2 + 2 * (rng() % ((HOR_RES - x0) / 2));
      int y1 = scene == 0 ? HOR_RES : y0 + 2 + 2 * (rng() % ((HOR_RES - y0) / 2));
      if (scene == 2) {
        x0 = 2;
        x1 = 4;
        y0 = 0;
        y1 = HOR_RES;
      }
      for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
          dirty[y * HOR_RES + x] = true;
    }
    for (int y = 0; y < HOR_RES; ++y) {
      for (int x = 0; x < HOR_RES;) {
        if (!dirty[y * HOR_RES + x]) {
          ++x;
          continue;
        }
        int x0 = x;
        while (x < HOR_RES && dirty[y * HOR_RES + x])
          ++x;
        segments.push_back({y, x0, x});
      }
    }
    salt = scene;
    for (bool sem : {false, true})
      for (bool automatic : {false, true})
        for (bool swap : {false, true}) {
          trans_sem = sem;
          automatic_completion = automatic;
          check(segments, swap);
          ++cases;
        }
  }
  std::printf("Passed %d frame cases: exact pixels, transfer bounds, DMA ownership, and async alignment.\n", cases);
}
