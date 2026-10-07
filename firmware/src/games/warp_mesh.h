#pragma once
#include <math.h>
#include <stdint.h>

// Deformable image layer: a continuous inverse-deformation mesh over a flat
// RGB565 image, driven by a rig that games describe as data. No ESP-IDF, Slint,
// Lua or heap dependencies, so host tests run exactly this code.
//
// A rig has soft regions and pins. Each region is one motion channel: the game
// sets its (dx, dy, squash) every frame, in source pixels. Its weight field is
//   patch: an ellipse with a plateau (r < 0.5) that moves as a unit and a wide
//          smooth falloff (to r = 1.7) that stretches the surrounding image,
//          optionally ramped toward its top ("anchor") so it swings from there;
//   blob:  up to four ellipses (combined by max) with a quadratic falloff.
// Regions are held still near pins (boxes, or stacked-row profiles open below),
// feathered over a distance; can keep clear of the patches of chosen groups;
// can yield to an earlier region ("under"); and share weight within a group
// where they overlap, instead of adding up. Everything fades out at the image
// border, which has no spare pixels to pull in.
//
// Each frame, pose() soft-limits each region's travel to what its falloff can
// absorb (patches) or to fixed pixel limits (blobs), applies an area-preserving
// squash about the region's top, then scales the whole field down if any mesh
// edge would stretch or shear by more than 0.45 of a cell, which keeps every
// cell's Jacobian positive: the image never folds. Vertices are quantized to
// 1/16 px so a settled layer stops re-rendering.
//
// Drawing only touches cells whose corners moved since the target buffer was
// last drawn. plan() + draw_planned_shared() split one frame between threads.
namespace warp
{
// The panel's pixel: RGB565, big-endian on the wire.
struct Rgb565BE {
  uint16_t v;
  Rgb565BE() = default;
  Rgb565BE(uint8_t r, uint8_t g, uint8_t b) {
    const uint16_t c = uint16_t((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
    v = uint16_t(c << 8 | c >> 8);
  }
};
static_assert(sizeof(Rgb565BE) == 2, "pushed to the panel as uint16_t");
// Stores a native RGB565 value: straight into the panel format, or expanded
// into any {r, g, b} 8-bit pixel.
inline void store565(Rgb565BE &out, uint16_t c) {
  out.v = uint16_t(c << 8 | c >> 8);
}
template <typename Pixel> inline void store565(Pixel &out, uint16_t c) {
  const uint8_t r = uint8_t(c >> 11), g = uint8_t(c >> 5 & 63), b = uint8_t(c & 31);
  out = {uint8_t(r << 3 | r >> 2), uint8_t(g << 2 | g >> 4), uint8_t(b << 3 | b >> 2)};
}
inline uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
}

// Smooth saturation: linear for small values, approaches +-limit.
inline float soft_limit(float value, float limit) {
  return limit > 0 ? limit * tanhf(value / limit) : 0;
}

constexpr int kCell = 8;
constexpr int kMaxSide = 352; // px, either dimension
constexpr int kMaxVertexSide = kMaxSide / kCell + 1;
constexpr int kMaxVertices = kMaxVertexSide * kMaxVertexSide;
constexpr int kMaxCells = (kMaxVertexSide - 1) * (kMaxVertexSide - 1);
constexpr int kMaxRegions = 8, kMaxPins = 8, kMaxBlobs = 4, kMaxProfileRows = 6;
// Patch shape: plateau to r = 0.5, smooth falloff to r = 1.7 (ellipse-normalized).
constexpr float kPlateau = 0.5f, kReach = 1.7f;
// Width of the still ring kept between a region and the patches it clears.
constexpr float kClearance = 0.9f;

struct Ellipse {
  float x, y, rx, ry;
  bool visible() const {
    return rx > 0 && ry > 0;
  }
};
struct Pin {
  // 0: the box below. 2..kMaxProfileRows: a profile of rows with strictly
  // increasing y, interpolated between rows and extended below the last one.
  int rows = 0;
  float left = 0, top = 0, right = 0, bottom = 0;
  struct Row {
    float y, left, right;
  } row[kMaxProfileRows] = {};
};
struct Region {
  enum Kind : uint8_t {
    Patch,
    Blob
  };
  uint8_t kind = Patch;
  uint8_t group = 0;         // 1..8: weight is shared with the group's other regions
  int8_t under = -1;         // yields to this earlier region: w *= 1 - w[under]
  uint8_t ellipse_count = 0; // patch: 1; blob: 0..kMaxBlobs
  Ellipse ellipses[kMaxBlobs] = {};
  // Patch only: weight rises from `anchor` at y = center + ry * anchor_top to 1
  // at center + ry * anchor_bottom. 1 disables the ramp.
  float anchor = 1, anchor_top = 0, anchor_bottom = 0;
  uint8_t pins = 0;                 // bitmask of rig pins holding this region still
  float feather = 0;                // px over which it fades in away from those pins
  uint8_t clear = 0;                // bitmask of groups (bit g - 1) whose patches it keeps clear of
  float travel = 1;                 // patch: fraction of its fold-free travel budget
  float travel_x = 0, travel_y = 0; // blob: soft travel limits, px
  float squash = 0;                 // soft limit of the squash fraction; 0 disables
};
struct Rig {
  float edge = 0; // px over which weights fade out toward the image border; 0: none
  int pin_count = 0;
  Pin pins[kMaxPins];
  int region_count = 0;
  Region regions[kMaxRegions];
};
// Per-region input for one frame, in source pixels; squash > 0 stretches vertically.
struct Motion {
  float dx, dy, squash;
};

// The per-pixel inner loop of the unclipped path: `count` pixels, source
// position (ax >> 3, ay >> 3) in 1/256 px advancing by (step_x, step_y) / 8.
// A SIMD build defines WARP_SPAN_SIMD and supplies warp_span_565() for the
// firmware's RGB565 -> panel combination with exactly these semantics.
#if defined(WARP_SPAN_SIMD)
extern "C" void warp_span_565(uint16_t *out, const uint16_t *source, int stride, int ax, int ay, int step_x, int step_y,
                              int count);
#endif

struct Mesh {
  static constexpr int cell = kCell;
  int width = 0, height = 0; // image size, px (multiples of cell)
  int columns = 1, rows = 1; // vertices per row and per column
  Rig rig{};
  float weights[kMaxVertices][kMaxRegions];
  struct Point {
    int x, y;
  } points[kMaxVertices]{}; // 1/256 px
  struct Offset {
    float x, y;
  } offsets[kMaxVertices];
  // Vertices with any weight; every other vertex never moves, so posing and the
  // fold guard only visit these.
  uint16_t active[kMaxVertices];
  int active_count = 0;
  // Cells with any moving corner: the only cells that can ever go stale.
  uint16_t active_cells[kMaxCells];
  int active_cell_count = 0;
  float last_limit_scale = 1; // < 1 when the fold guard had to intervene
  bool pose_dirty = true;

  int cell_columns() const {
    return columns - 1;
  }
  int cell_rows() const {
    return rows - 1;
  }
  static bool valid_size(int w, int h) {
    return w >= cell && h >= cell && w <= kMaxSide && h <= kMaxSide && w % cell == 0 && h % cell == 0;
  }

  static float smooth(float lo, float hi, float value) {
    const float t = fmaxf(0, fminf(1, (value - lo) / (hi - lo)));
    return t * t * (3 - 2 * t);
  }
  static float blob(float x, float y, const Ellipse &e) {
    if (!e.visible())
      return 0;
    x = (x - e.x) / e.rx;
    y = (y - e.y) / e.ry;
    const float w = fmaxf(0, 1 - x * x - y * y);
    return w * w;
  }
  static float radius(float x, float y, const Ellipse &e) {
    const float dx = (x - e.x) / e.rx, dy = (y - e.y) / e.ry;
    return sqrtf(dx * dx + dy * dy);
  }
  static float soft_patch(float x, float y, const Ellipse &e) {
    return e.visible() ? 1 - smooth(kPlateau, kReach, radius(x, y, e)) : 0;
  }
  // Travel a patch can take while its own displacement gradient stays near
  // 0.28, leaving headroom for squash and the anchor ramp.
  static float patch_limit(const Ellipse &e) {
    const float falloff = (kReach - kPlateau) * fminf(e.rx, e.ry);
    return 0.28f * falloff / 1.5f; // smoothstep peak slope is 1.5 / width
  }
  // Signed distance-like measure: > 0 outside the pin, <= 0 inside.
  static float outside(float x, float y, const Pin &p) {
    if (p.rows < 2)
      return fmaxf(fmaxf(p.left - x, x - p.right), fmaxf(p.top - y, y - p.bottom));
    float left = p.row[0].left, right = p.row[0].right;
    for (int i = 0; i + 1 < p.rows; ++i) {
      const float t = fmaxf(0, fminf(1, (y - p.row[i].y) / (p.row[i + 1].y - p.row[i].y)));
      left += (p.row[i + 1].left - p.row[i].left) * t;
      right += (p.row[i + 1].right - p.row[i].right) * t;
    }
    return fmaxf(p.row[0].y - y, fmaxf(left - x, x - right));
  }
  // Inline rounding; lrintf is a library call on the ESP32-S3.
  static int round_to_int(float v) {
    return int(v >= 0 ? v + 0.5f : v - 0.5f);
  }

  // Structural checks the firmware's rig parser also enforces. Geometry is free.
  static bool valid_rig(const Rig &r) {
    if (r.pin_count < 0 || r.pin_count > kMaxPins || r.region_count < 0 || r.region_count > kMaxRegions ||
        !(r.edge >= 0))
      return false;
    for (int i = 0; i < r.pin_count; ++i) {
      const Pin &p = r.pins[i];
      if (p.rows == 1 || p.rows < 0 || p.rows > kMaxProfileRows)
        return false;
      for (int k = 0; k + 1 < p.rows; ++k)
        if (!(p.row[k + 1].y > p.row[k].y))
          return false;
    }
    for (int i = 0; i < r.region_count; ++i) {
      const Region &g = r.regions[i];
      if (g.kind > Region::Blob || g.group > 8 || g.under >= i || g.under < -1)
        return false;
      if (g.kind == Region::Patch ? g.ellipse_count != 1 : g.ellipse_count > kMaxBlobs)
        return false;
      if ((g.pins >> r.pin_count) != 0 || (g.pins && !(g.feather > 0)))
        return false;
      if (!(g.anchor >= 0 && g.anchor <= 1) || (g.anchor < 1 && !(g.anchor_bottom > g.anchor_top)))
        return false;
      if (!(g.travel >= 0) || !(g.travel_x >= 0) || !(g.travel_y >= 0) || !(g.squash >= 0 && g.squash < 1))
        return false;
    }
    return true;
  }

  // Builds the weight fields for a width x height image. Returns false (and
  // leaves the mesh empty and still) if the size or rig is invalid.
  bool configure(int w, int h, const Rig &r) {
    pose_dirty = true;
    rendered_valid[0] = rendered_valid[1] = false;
    const bool ok = valid_size(w, h) && valid_rig(r);
    width = ok ? w : cell;
    height = ok ? h : cell;
    columns = width / cell + 1;
    rows = height / cell + 1;
    rig = r;
    if (!ok)
      rig.region_count = rig.pin_count = 0;
    const int regions = rig.region_count;

    for (int row = 0; row < rows; ++row) {
      for (int col = 0; col < columns; ++col) {
        const float x = float(col * cell), y = float(row * cell);
        float *wv = weights[row * columns + col];
        // Radius in each visible patch, for clearance rings.
        float r_patch[kMaxRegions];
        for (int i = 0; i < regions; ++i) {
          const Region &g = rig.regions[i];
          r_patch[i] = g.kind == Region::Patch && g.ellipses[0].visible() ? radius(x, y, g.ellipses[0]) : 1e9f;
        }
        for (int i = 0; i < regions; ++i) {
          const Region &g = rig.regions[i];
          float shape = 0;
          if (g.kind == Region::Patch) {
            const Ellipse &e = g.ellipses[0];
            shape = soft_patch(x, y, e);
            if (shape > 0 && g.anchor < 1)
              shape *= g.anchor + (1 - g.anchor) * smooth(e.y + e.ry * g.anchor_top, e.y + e.ry * g.anchor_bottom, y);
          }
          else {
            for (int k = 0; k < g.ellipse_count; ++k)
              shape = fmaxf(shape, blob(x, y, g.ellipses[k]));
          }
          float factor = 1;
          if (g.pins) {
            float distance = 1e9f;
            for (int p = 0; p < rig.pin_count; ++p)
              if (g.pins >> p & 1)
                distance = fminf(distance, outside(x, y, rig.pins[p]));
            factor = smooth(0, g.feather, distance);
          }
          if (g.clear) {
            float nearest = 1e9f;
            for (int q = 0; q < regions; ++q) {
              const int group = rig.regions[q].group;
              if (group && (g.clear >> (group - 1) & 1))
                nearest = fminf(nearest, r_patch[q]);
            }
            factor *= smooth(kReach, kReach + kClearance, nearest);
          }
          wv[i] = shape * factor;
        }
        // Where regions of one group overlap, blend rather than add.
        for (int group = 1; group <= 8; ++group) {
          float total = 0;
          for (int i = 0; i < regions; ++i)
            if (rig.regions[i].group == group)
              total += wv[i];
          if (total > 1)
            for (int i = 0; i < regions; ++i)
              if (rig.regions[i].group == group)
                wv[i] /= total;
        }
        for (int i = 0; i < regions; ++i)
          if (rig.regions[i].under >= 0)
            wv[i] *= 1 - wv[rig.regions[i].under];
        const float edge = rig.edge > 0 ? smooth(0, rig.edge, fminf(fminf(x, width - x), fminf(y, height - y))) : 1;
        for (int i = 0; i < regions; ++i)
          wv[i] *= edge;
        for (int i = regions; i < kMaxRegions; ++i)
          wv[i] = 0;
      }
    }
    active_count = 0;
    for (int i = 0; i < columns * rows; ++i) {
      offsets[i] = {0, 0};
      points[i] = {(i % columns) * cell * 256, (i / columns) * cell * 256};
      if (moves(i))
        active[active_count++] = uint16_t(i);
    }
    active_cell_count = 0;
    for (int row = 0; row < rows - 1; ++row) {
      for (int col = 0; col < columns - 1; ++col) {
        const int i = row * columns + col;
        if (moves(i) || moves(i + 1) || moves(i + columns) || moves(i + columns + 1))
          active_cells[active_cell_count++] = uint16_t(row * (columns - 1) + col);
      }
    }
    last_limit_scale = 1;
    return ok;
  }
  bool moves(int vertex) const {
    for (int i = 0; i < rig.region_count; ++i)
      if (weights[vertex][i] > 0)
        return true;
    return false;
  }

  // Builds the mesh for this frame's motion (rig.region_count entries; missing
  // or non-finite values count as zero). Returns false when the mesh is
  // unchanged and the last frame can be reused.
  bool pose(const Motion *motion) {
    struct Placed {
      float dx, dy, sx, sy, ax, ay;
    } placed[kMaxRegions];
    for (int i = 0; i < rig.region_count; ++i) {
      const Region &g = rig.regions[i];
      const Ellipse &e = g.ellipses[0];
      const auto finite = [](float v) { return isfinite(v) ? v : 0.0f; };
      float dx = motion ? finite(motion[i].dx) : 0, dy = motion ? finite(motion[i].dy) : 0;
      if (g.kind == Region::Patch) {
        // Limit the travel vector, not each axis, so diagonals stay in budget.
        const float travel = sqrtf(dx * dx + dy * dy);
        const float keep = travel > 0.001f ? soft_limit(travel, patch_limit(e) * g.travel) / travel : 1;
        dx *= keep;
        dy *= keep;
      }
      else {
        dx = soft_limit(dx, g.travel_x);
        dy = soft_limit(dy, g.travel_y);
      }
      // Area-preserving squash about the region's top.
      const float s = soft_limit(motion ? finite(motion[i].squash) : 0, g.squash);
      placed[i] = {dx, dy, 1 / (1 + s), 1 + s, e.x, e.y - e.ry};
    }

    for (int k = 0; k < active_count; ++k) {
      const int index = active[k];
      const float *w = weights[index];
      const float x = float(index % columns * cell), y = float(index / columns * cell);
      float ox = 0, oy = 0;
      for (int i = 0; i < rig.region_count; ++i) {
        if (w[i] <= 0)
          continue;
        const Placed &p = placed[i];
        ox += (p.dx + (x - p.ax) * (p.sx - 1)) * w[i];
        oy += (p.dy + (y - p.ay) * (p.sy - 1)) * w[i];
      }
      offsets[index] = {ox, oy};
    }

    // Fold guard. If no mesh edge stretches or shears by more than 0.45 of a
    // cell, every cell's Jacobian determinant stays above 1 - 2 * 0.45 > 0, so
    // the map cannot fold. Per-region limits normally keep it well below.
    // Only edges touching a moving vertex can stretch.
    float max_edge_squared = 0;
    for (int k = 0; k < active_count; ++k) {
      const int index = active[k];
      const int col = index % columns, row = index / columns;
      const auto &a = offsets[index];
      const int neighbours[4] = {col < columns - 1 ? index + 1 : -1, col > 0 ? index - 1 : -1,
                                 row < rows - 1 ? index + columns : -1, row > 0 ? index - columns : -1};
      for (int n : neighbours) {
        if (n < 0)
          continue;
        const auto &b = offsets[n];
        max_edge_squared = fmaxf(max_edge_squared, (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
      }
    }
    const float max_edge = sqrtf(max_edge_squared);
    const float scale = max_edge > cell * 0.45f ? cell * 0.45f / max_edge : 1;
    last_limit_scale = scale;

    bool changed = pose_dirty;
    for (int k = 0; k < active_count; ++k) {
      const int index = active[k];
      const int col = index % columns, row = index / columns;
      // Quantized to 1/16 px so a settled layer stops re-rendering.
      const Point next = {round_to_int((col * cell - offsets[index].x * scale) * 16) * 16,
                          round_to_int((row * cell - offsets[index].y * scale) * 16) * 16};
      changed = changed || points[index].x != next.x || points[index].y != next.y;
      points[index] = next;
    }
    pose_dirty = false;
    return changed;
  }

  // Mesh last written into each output buffer, so a frame only redraws the
  // cells that moved since that buffer was drawn. configure() invalidates both.
  Point rendered[2][kMaxVertices];
  bool rendered_valid[2] = {false, false};
  int last_cells_drawn = 0;
  // Which cells the last plan() or render_region() marked, row-major.
  uint8_t drawn[kMaxCells];

  // True when the cells [col0, col0 + cols) x [row0, row0 + rows_) would draw
  // exactly what `previous` (that region's vertices, as last drawn) produced.
  bool region_matches(int col0, int row0, int cols, int rows_, const Point *previous) const {
    for (int r = 0; r <= rows_; ++r) {
      const Point *now = points + (row0 + r) * columns + col0;
      const Point *was = previous + r * (cols + 1);
      for (int c = 0; c <= cols; ++c)
        if (now[c].x != was[c].x || now[c].y != was[c].y)
          return false;
    }
    return true;
  }

  // Bilinear sample with fractional position (fx, wy) in 1/256 px; the right and
  // lower neighbours are p[right] and p[below]. Two source formats:
  //
  // 0x00RRGGBB words: two-lane blend, red and blue share one word, green another.
  static uint32_t pack(uint8_t r, uint8_t g, uint8_t b) {
    return uint32_t(r) << 16 | uint32_t(g) << 8 | b;
  }
  template <typename Target>
  static void sample(Target &out, const uint32_t *p, int right, int below, uint32_t fx, uint32_t wy) {
    const uint32_t p00 = p[0], p01 = p[right], p10 = p[below], p11 = p[below + right];
    const uint32_t top_rb = ((p00 & 0xFF00FF) * (256 - fx) + (p01 & 0xFF00FF) * fx) >> 8 & 0xFF00FF;
    const uint32_t top_g = ((p00 & 0xFF00) * (256 - fx) + (p01 & 0xFF00) * fx) >> 8 & 0xFF00;
    const uint32_t bot_rb = ((p10 & 0xFF00FF) * (256 - fx) + (p11 & 0xFF00FF) * fx) >> 8 & 0xFF00FF;
    const uint32_t bot_g = ((p10 & 0xFF00) * (256 - fx) + (p11 & 0xFF00) * fx) >> 8 & 0xFF00;
    const uint32_t rb = (top_rb * (256 - wy) + bot_rb * wy) >> 8 & 0xFF00FF;
    const uint32_t g = (top_g * (256 - wy) + bot_g * wy) >> 8 & 0xFF00;
    out = {uint8_t(rb >> 16), uint8_t(g >> 8), uint8_t(rb)};
  }
  // Native RGB565 halfwords, what the firmware uses: half the memory traffic of
  // words, and the blend happens in the panel's own precision. Spreading the
  // value to 0x07E0F81F gives each channel its own lane in one word, so a single
  // multiply per neighbour blends all three with 5-bit weights (1/32 px, finer
  // than 565 can show).
  template <typename Target>
  static void sample(Target &out, const uint16_t *p, int right, int below, uint32_t fx, uint32_t wy) {
    constexpr uint32_t lanes = 0x07E0F81F;
    const uint32_t wx = fx >> 3, wv = wy >> 3;
    const uint32_t p00 = (p[0] | uint32_t(p[0]) << 16) & lanes, p01 = (p[right] | uint32_t(p[right]) << 16) & lanes;
    const uint32_t p10 = (p[below] | uint32_t(p[below]) << 16) & lanes;
    const uint32_t p11 = (p[below + right] | uint32_t(p[below + right]) << 16) & lanes;
    const uint32_t top = (p00 * (32 - wx) + p01 * wx) >> 5 & lanes;
    const uint32_t bot = (p10 * (32 - wx) + p11 * wx) >> 5 & lanes;
    const uint32_t v = (top * (32 - wv) + bot * wv) >> 5 & lanes;
    store565(out, uint16_t(v | v >> 16));
  }

  // Unclipped span: every sample and its right/lower neighbours are inside.
  template <typename Source, typename Target>
  static void span_inside(Target *out, const Source *source, int stride, int ax, int ay, int step_x, int step_y,
                          int count) {
    for (int step = 0; step < count; ++step, ax += step_x, ay += step_y) {
      const int sx = ax >> 3, sy = ay >> 3;
      sample(out[step], source + (sy >> 8) * stride + (sx >> 8), 1, stride, uint32_t(sx & 255), uint32_t(sy & 255));
    }
  }
#if defined(WARP_SPAN_SIMD)
  static void span_inside(Rgb565BE *out, const uint16_t *source, int stride, int ax, int ay, int step_x, int step_y,
                          int count) {
    warp_span_565(&out->v, source, stride, ax, ay, step_x, step_y, count);
  }
#endif

  // Warps pixel row `fy` of one cell (global cell row/column) into `out`, that
  // row's first pixel. Reads only the mesh and the source, so different cells can
  // be drawn concurrently.
  template <typename Source, typename Target>
  void draw_span(const Source *source, Target *out, int row, int col, int fy) const {
    static_assert(cell == 8, "the inner loop divides by cell with >> 3");
    const int i = row * columns + col;
    const Point &a = points[i], &b = points[i + 1], &cc = points[i + columns], &d = points[i + columns + 1];
    // Both ends come from the shared cell edges, so neighbours agree exactly.
    const int start_x = a.x + ((cc.x - a.x) * fy >> 3), start_y = a.y + ((cc.y - a.y) * fy >> 3);
    const int span_x = b.x + ((d.x - b.x) * fy >> 3) - start_x;
    const int span_y = b.y + ((d.y - b.y) * fy >> 3) - start_y;
    // Running sums in 1/8 steps: (8 * start + span * step) >> 3 is exactly
    // start + (span * step >> 3), without a multiply per pixel.
    int ax = start_x * 8, ay = start_y * 8;
    // Coordinates are linear along the run, so if both ends are strictly inside
    // (a right and a lower neighbour exist), every pixel is: skip all per-pixel
    // bounds checks and edge clamps. Same results as the careful loop below.
    const int end_x = (ax + span_x * (cell - 1)) >> 3, end_y = (ay + span_y * (cell - 1)) >> 3;
    const int inner_x = (width - 1) * 256, inner_y = (height - 1) * 256;
    if (start_x >= 0 && end_x >= 0 && start_y >= 0 && end_y >= 0 && start_x < inner_x && end_x < inner_x &&
        start_y < inner_y && end_y < inner_y) {
      span_inside(out, source, width, ax, ay, span_x, span_y, cell);
      return;
    }
    for (int step = 0; step < cell; ++step, ax += span_x, ay += span_y) {
      const int sx = ax >> 3, sy = ay >> 3;
      if (sx < 0 || sy < 0 || sx > (width - 1) * 256 || sy > (height - 1) * 256) {
        out[step] = {0, 0, 0};
        continue;
      }
      const int ix = sx >> 8, iy = sy >> 8;
      sample(out[step], source + iy * width + ix, ix < width - 1 ? 1 : 0, iy < height - 1 ? width : 0,
             uint32_t(sx & 255), uint32_t(sy & 255));
    }
  }

  // Warps one whole cell into `out`, its top-left pixel, row pitch `stride`.
  template <typename Source, typename Target>
  void draw_cell(const Source *source, Target *out, int stride, int row, int col) const {
    for (int fy = 0; fy < cell; ++fy, out += stride)
      draw_span(source, out, row, col, fy);
  }

  // Draws the cells [col0, col0 + cols) x [row0, row0 + rows_) into `target`,
  // whose pixel (0, 0) is the region's top-left corner, with row pitch `stride`.
  // `previous` holds the region's (cols + 1) x (rows_ + 1) vertices as last drawn
  // into this target; only cells whose corners moved are redrawn, and it is
  // updated. Pass previous_valid = false (new image, new buffer) to draw it all.
  // Output is opaque RGB, so transparent edges cannot leave trails.
  template <typename Source, typename Target>
  int render_region(const Source *source, Target *target, int stride, int col0, int row0, int cols, int rows_,
                    Point *previous, bool previous_valid) {
    const int pitch = cols + 1;
    int cells_drawn = 0;
    for (int r = 0; r < rows_; ++r) {
      for (int c = 0; c < cols; ++c) {
        const int i = (row0 + r) * columns + col0 + c;
        const int j = r * pitch + c;
        const Point &a = points[i], &b = points[i + 1], &cc = points[i + columns], &d = points[i + columns + 1];
        if (previous && previous_valid && a.x == previous[j].x && a.y == previous[j].y && b.x == previous[j + 1].x &&
            b.y == previous[j + 1].y && cc.x == previous[j + pitch].x && cc.y == previous[j + pitch].y &&
            d.x == previous[j + pitch + 1].x && d.y == previous[j + pitch + 1].y)
          continue;
        ++cells_drawn;
        drawn[(row0 + r) * cell_columns() + col0 + c] = 1;
        draw_cell(source, target + r * cell * stride + c * cell, stride, row0 + r, col0 + c);
      }
    }
    if (previous) {
      for (int r = 0; r <= rows_; ++r)
        for (int c = 0; c <= cols; ++c)
          previous[r * pitch + c] = points[(row0 + r) * columns + col0 + c];
    }
    return cells_drawn;
  }

  // Whole-image rendering in two steps, so the drawing can be split between
  // threads. plan() decides, single-threaded, which cells of buffer `slot` (0 or
  // 1, alternating; -1 for an untracked full redraw) are stale, marks them in
  // drawn[] and records the mesh as drawn. draw_planned() then draws the marked
  // cells of cell rows [row_begin, row_end); disjoint ranges may run concurrently.
  int plan(int slot) {
    const int cells = cell_columns() * cell_rows();
    Point *previous = slot == 0 || slot == 1 ? rendered[slot] : nullptr;
    const bool incremental = previous && rendered_valid[slot];
    int n = 0;
    if (!incremental) {
      for (int c = 0; c < cells; ++c)
        drawn[c] = 1;
      n = cells;
      if (previous) {
        for (int i = 0; i < columns * rows; ++i)
          previous[i] = points[i];
        rendered_valid[slot] = true;
      }
      last_cells_drawn = n;
      return n;
    }
    // Only cells with a moving corner can be stale, and only moving vertices can
    // differ from what this buffer was drawn with.
    for (int c = 0; c < cells; ++c)
      drawn[c] = 0;
    for (int k = 0; k < active_cell_count; ++k) {
      const int c = active_cells[k];
      const int i = c / cell_columns() * columns + c % cell_columns();
      const int corners[4] = {i, i + 1, i + columns, i + columns + 1};
      for (int v : corners) {
        if (points[v].x != previous[v].x || points[v].y != previous[v].y) {
          drawn[c] = 1;
          ++n;
          break;
        }
      }
    }
    for (int k = 0; k < active_count; ++k)
      previous[active[k]] = points[active[k]];
    last_cells_drawn = n;
    return n;
  }

  // Scanline order, not cell order: each output row visits every marked cell on
  // it before moving down, so the working set is about one output row and two
  // source rows.
  template <typename Source, typename Target>
  void draw_planned(const Source *source, Target *target, int row_begin, int row_end) const {
    const int cols = cell_columns();
    for (int row = row_begin; row < row_end; ++row) {
      const uint8_t *marked = drawn + row * cols;
      int first = 0, last = cols - 1;
      while (first < cols && !marked[first])
        ++first;
      if (first == cols)
        continue;
      while (!marked[last])
        --last;
      for (int fy = 0; fy < cell; ++fy) {
        Target *line = target + (row * cell + fy) * width;
        for (int col = first; col <= last; ++col)
          if (marked[col])
            draw_span(source, line + col * cell, row, col, fy);
      }
    }
  }

  // Shared drawing: every caller passes the same counter (starting at 0) and
  // takes the next cell row until none are left, so whichever core has more
  // time free does more of the work. Returns the rows this caller drew.
  template <typename Source, typename Target, typename Counter>
  int draw_planned_shared(const Source *source, Target *target, Counter &next_row) const {
    int done = 0;
    for (int row; (row = next_row.fetch_add(1)) < cell_rows(); ++done)
      draw_planned(source, target, row, row + 1);
    return done;
  }

  template <typename Source, typename Target> void render(const Source *source, Target *target, int slot = -1) {
    plan(slot);
    draw_planned(source, target, 0, cell_rows());
  }
};
} // namespace warp
