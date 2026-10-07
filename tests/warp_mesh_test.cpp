// Host tests for the deformable image layer (firmware/src/games/warp_mesh.h),
// using standalone regression rigs in tests/fixtures/mascot_rigs.txt.
// No Lua package or portrait assets are required to run these tests.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "games/warp_mesh.h"
#include <algorithm>
#include <assert.h>
#include <atomic>
#include <limits>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>
#include <vector>

#ifndef WARP_FIXTURE
#define WARP_FIXTURE "tests/fixtures/mascot_rigs.txt"
#endif

namespace {
constexpr int side = 352;
constexpr int n = side / warp::kCell + 1;
using Point = warp::Mesh::Point;

struct Look {
  int character, pose, outfit;
  warp::Rig rig;
};

// Reads the whitespace-separated renderer regression fixture.
std::vector<Look> read_fixture(const char *path) {
  FILE *file = fopen(path, "r");
  if (!file) {
    fprintf(stderr, "Cannot open %s\n", path);
    exit(1);
  }
  std::vector<Look> looks;
  char word[32];
  const auto number = [&]() {
    float v;
    if (fscanf(file, "%f", &v) != 1) exit(2);
    return v;
  };
  const auto integer = [&]() {
    int v;
    if (fscanf(file, "%d", &v) != 1) exit(2);
    return v;
  };
  while (fscanf(file, "%31s", word) == 1) {
    if (word[0] == '#') {
      int c;
      while ((c = fgetc(file)) != '\n' && c != EOF) {}
    }
    else if (!strcmp(word, "look")) {
      looks.push_back({});
      looks.back().character = integer();
      looks.back().pose = integer();
      looks.back().outfit = integer();
    }
    else if (!strcmp(word, "rig")) {
      warp::Rig &r = looks.back().rig;
      r.edge = number();
      r.pin_count = 0;
      r.region_count = 0;
      const int pins = integer(), regions = integer();
      for (int i = 0; i < pins; ++i) {
        warp::Pin &p = r.pins[r.pin_count++];
        if (fscanf(file, "%31s %31s", word, word) != 2) exit(2);
        if (!strcmp(word, "box")) {
          p.rows = 0;
          p.left = number();
          p.top = number();
          p.right = number();
          p.bottom = number();
        }
        else {
          p.rows = integer();
          for (int k = 0; k < p.rows; ++k) p.row[k] = {number(), number(), number()};
        }
      }
      for (int i = 0; i < regions; ++i) {
        warp::Region &g = r.regions[r.region_count++];
        if (fscanf(file, "%31s %31s", word, word) != 2) exit(2);
        g.kind = !strcmp(word, "patch") ? warp::Region::Patch : warp::Region::Blob;
        g.group = uint8_t(integer());
        g.under = int8_t(integer());
        g.anchor = number();
        g.anchor_top = number();
        g.anchor_bottom = number();
        g.pins = uint8_t(integer());
        g.feather = number();
        g.clear = uint8_t(integer());
        g.travel = number();
        g.travel_x = number();
        g.travel_y = number();
        g.squash = number();
        g.ellipse_count = uint8_t(integer());
        for (int k = 0; k < g.ellipse_count; ++k) g.ellipses[k] = {number(), number(), number(), number()};
      }
    }
  }
  fclose(file);
  return looks;
}

struct Rgb {
  uint8_t r, g, b;
};
bool same(uint32_t p, const Rgb &q) { return (p >> 16 & 255) == q.r && (p >> 8 & 255) == q.g && (p & 255) == q.b; }
bool same(const Rgb &a, const Rgb &b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// Deterministic, physics-like motion: each region swings on its own, with a
// jolt now and then, at up to `amplitude` px (and squash in proportion).
struct Driver {
  warp::Motion motion[warp::kMaxRegions] = {};
  uint32_t seed = 1;
  float random() {
    seed = seed * 1664525u + 1013904223u;
    return float(seed >> 8) / float(1u << 24) * 2 - 1;
  }
  const warp::Motion *at(int frame, float amplitude) {
    for (int i = 0; i < warp::kMaxRegions; ++i) {
      const float t = frame * 0.11f + i * 1.3f;
      motion[i] = {amplitude * sinf(t * (1 + 0.1f * i)), amplitude * cosf(t * 0.8f),
                   amplitude * 0.01f * sinf(t * 1.7f)};
    }
    return motion;
  }
  const warp::Motion *wild(float amplitude) {
    for (auto &m : motion) m = {amplitude * random(), amplitude * random(), amplitude * random()};
    return motion;
  }
  const warp::Motion *still() {
    for (auto &m : motion) m = {0, 0, 0};
    return motion;
  }
};

bool soft_vertex(const warp::Mesh &mesh, int i) {
  for (int k = 0; k < mesh.rig.region_count; ++k)
    if (mesh.weights[i][k] > 0) return true;
  return false;
}

void assert_fold_free(const warp::Mesh &mesh) {
  for (int row = 0; row < mesh.rows - 1; ++row) {
    for (int col = 0; col < mesh.columns - 1; ++col) {
      const int c = mesh.columns;
      const auto &a = mesh.points[row * c + col], &b = mesh.points[row * c + col + 1];
      const auto &cc = mesh.points[(row + 1) * c + col], &d = mesh.points[(row + 1) * c + col + 1];
      const float unit = warp::kCell * 256.0f;
      for (int corner = 0; corner < 4; ++corner) {
        const float xx = (corner & 2 ? d.x - cc.x : b.x - a.x) / unit;
        const float xy = (corner & 1 ? d.x - b.x : cc.x - a.x) / unit;
        const float yx = (corner & 2 ? d.y - cc.y : b.y - a.y) / unit;
        const float yy = (corner & 1 ? d.y - b.y : cc.y - a.y) / unit;
        assert(xx * yy - xy * yx > 0.1f);
      }
    }
  }
}

void mesh_tests(const std::vector<Look> &looks) {
  static warp::Mesh mesh;
  Driver drive;
  std::vector<uint32_t> source(side * side);
  std::vector<Rgb> target(side * side + 2);
  target.front() = {17, 23, 31};
  target.back() = {41, 47, 53};
  for (int y = 0; y < side; ++y)
    for (int x = 0; x < side; ++x)
      source[y * side + x] = warp::Mesh::pack(uint8_t(x % 251), uint8_t(y % 251), uint8_t((x + y) % 251));

  // Unchanged meshes skip rendering; a new rig forces one frame.
  {
    assert(mesh.configure(side, side, looks[0].rig));
    assert(mesh.pose(drive.still()));
    assert(!mesh.pose(drive.still()));
    assert(mesh.pose(drive.at(3, 12)));
    assert(!mesh.pose(drive.motion));
    assert(mesh.pose(drive.still()));
    assert(!mesh.pose(nullptr));
    mesh.configure(side, side, looks[1].rig);
    assert(mesh.pose(nullptr));
  }

  // At rest, and for non-finite input, the output is the source exactly, edges included.
  assert(mesh.configure(side, side, looks[0].rig));
  mesh.pose(drive.still());
  mesh.render(source.data(), target.data() + 1);
  for (int i = 0; i < side * side; ++i) assert(same(source[i], target[i + 1]));
  {
    warp::Motion bad[warp::kMaxRegions];
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (auto &m : bad) m = {nan, inf, -inf};
    mesh.pose(bad);
    mesh.render(source.data(), target.data() + 1);
    for (int i = 0; i < side * side; ++i) assert(same(source[i], target[i + 1]));
  }

  for (const Look &look : looks) {
    assert(mesh.configure(side, side, look.rig));
    const warp::Rig &rig = mesh.rig;
    // The package's rigs: hair, jacket, bust left/right, rear left/right; pins
    // face, neck, hands and the torso core.
    assert(rig.region_count == 6 && rig.pin_count == 5);
    // Each bust patch is either occluded (zero-size) or inside the canvas with
    // real extent; front and turned looks show both, back shows one.
    int visible = 0;
    for (int r = 2; r < 4; ++r) {
      const auto &e = rig.regions[r].ellipses[0];
      if (!e.visible()) continue;
      ++visible;
      assert(e.rx >= 10 && e.ry >= 19);
      assert(e.x - e.rx > 0 && e.x + e.rx < side && e.y - e.ry > 0 && e.y + e.ry < side);
    }
    assert(visible == (look.pose == 2 ? 1 : 2));
    // The rear shows only where it is visible: one side turned, both from behind.
    int cheeks = 0;
    for (int r = 4; r < 6; ++r) {
      const auto &e = rig.regions[r].ellipses[0];
      if (!e.visible()) {
        for (int i = 0; i < n * n; ++i) assert(mesh.weights[i][r] == 0);
        continue;
      }
      ++cheeks;
      assert(e.rx >= 20 && e.ry >= 30 && e.x - e.rx > 0 && e.x + e.rx < side);
    }
    assert(cheeks == look.pose);
    // Only the riding suit has an open jacket.
    if (look.outfit != 0)
      for (int i = 0; i < n * n; ++i) assert(mesh.weights[i][1] == 0);

    // Moving one patch moves it, and nothing outside the soft regions: pinned
    // parts, and everything well away from the regions, stay pixel-identical.
    for (int r = 2; r < 6; ++r) {
      const auto &e = rig.regions[r].ellipses[0];
      if (!e.visible()) continue;
      warp::Motion motion[warp::kMaxRegions] = {};
      motion[r] = {9.6f, 12.8f, 0.16f};
      mesh.pose(motion);
      mesh.render(source.data(), target.data() + 1);
      int moved = 0;
      for (int y = int(e.y - e.ry); y < int(e.y + e.ry) && y < side; ++y)
        for (int x = int(e.x - e.rx); x < int(e.x + e.rx); ++x)
          if (!same(source[y * side + x], target[y * side + x + 1])) ++moved;
      assert(moved > int(e.rx * e.ry));
      for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
          const int row = y / warp::kCell, col = x / warp::kCell;
          bool soft = false;
          for (int o : {0, 1, n, n + 1}) soft = soft || soft_vertex(mesh, row * n + col + o);
          if (!soft) assert(same(source[y * side + x], target[y * side + x + 1]));
        }
      }
    }

    // Violent motion at any strength never folds a cell, never moves the
    // image border, and never writes outside the target.
    for (int frame = 0; frame < 240; ++frame) {
      mesh.pose(frame % 3 ? drive.wild(150) : drive.at(frame, 60));
      for (int col = 0; col < n; ++col) assert(mesh.points[col].y == 0);
      assert_fold_free(mesh);
    }
    mesh.render(source.data(), target.data() + 1);
    assert(target.front().r == 17 && target.back().b == 53);

    // Pins hold: vertices inside any pin of a region have none of its weight.
    int checked = 0;
    for (int r = 0; r < rig.region_count; ++r) {
      for (int p = 0; p < rig.pin_count; ++p) {
        if (!(rig.regions[r].pins >> p & 1)) continue;
        for (int i = 0; i < n * n; ++i) {
          const float x = float(i % n * warp::kCell), y = float(i / n * warp::kCell);
          if (warp::Mesh::outside(x, y, rig.pins[p]) > 0) continue;
          assert(mesh.weights[i][r] == 0);
          ++checked;
        }
      }
    }
    assert(checked > 50);
  }

  // Redrawing only the moved cells into alternating buffers is byte-identical
  // to a full redraw, and a still layer redraws nothing.
  {
    std::vector<Rgb> slots[2] = {std::vector<Rgb>(side * side), std::vector<Rgb>(side * side)};
    std::vector<Rgb> full(side * side);
    mesh.configure(side, side, looks[2].rig);
    int redrawn = 0;
    for (int frame = 0; frame < 150; ++frame) {
      mesh.pose(frame < 100 ? drive.at(frame, frame >= 40 && frame < 60 ? 14.0f : 5.0f) : drive.still());
      auto &slot = slots[frame % 2];
      mesh.render(source.data(), slot.data(), frame % 2);
      redrawn += mesh.last_cells_drawn;
      mesh.render(source.data(), full.data());
      for (int i = 0; i < side * side; ++i) assert(same(slot[i], full[i]));
    }
    const int all = (n - 1) * (n - 1);
    assert(redrawn < all * 150 / 2); // most frames touch a fraction of the cells
    mesh.pose(drive.still());
    mesh.render(source.data(), slots[0].data(), 0);
    mesh.render(source.data(), slots[1].data(), 1);
    mesh.pose(drive.still());
    mesh.render(source.data(), slots[0].data(), 0);
    assert(mesh.last_cells_drawn == 0);
    // A new rig invalidates both buffers.
    mesh.configure(side, side, looks[11].rig);
    mesh.pose(drive.still());
    mesh.render(source.data(), slots[0].data(), 0);
    assert(mesh.last_cells_drawn == all);
  }

  // The firmware's tile path: a 4x4 grid of double-buffered tiles, each redrawn
  // into its back buffer only when the shown buffer is stale. The shown tiles
  // always assemble to exactly the full-frame render.
  {
    constexpr int tiles = 4, tile = side / tiles, tile_cells = tile / warp::kCell;
    std::vector<Rgb> buffers[tiles * tiles][2];
    std::vector<Point> seen[tiles * tiles][2];
    bool valid[tiles * tiles][2] = {};
    int back[tiles * tiles] = {};
    for (auto &pair : buffers) pair[0].resize(tile * tile), pair[1].resize(tile * tile);
    for (auto &pair : seen)
      pair[0].resize((tile_cells + 1) * (tile_cells + 1)), pair[1].resize((tile_cells + 1) * (tile_cells + 1));
    std::vector<Rgb> full(side * side);
    mesh.configure(side, side, looks[7].rig);
    int republished = 0, republished_late = 0;
    for (int frame = 0; frame < 300; ++frame) {
      mesh.pose(frame < 200 ? drive.at(frame, frame >= 30 && frame < 45 ? 12.0f : 4.0f) : drive.still());
      for (int t = 0; t < tiles * tiles; ++t) {
        const int col0 = (t % tiles) * tile_cells, row0 = (t / tiles) * tile_cells;
        const int shown = back[t] ^ 1;
        if (frame > 0 && valid[t][shown] && mesh.region_matches(col0, row0, tile_cells, tile_cells, seen[t][shown].data()))
          continue;
        mesh.render_region(source.data(), buffers[t][back[t]].data(), tile, col0, row0, tile_cells, tile_cells,
                           seen[t][back[t]].data(), valid[t][back[t]]);
        valid[t][back[t]] = true;
        back[t] = shown;
        ++republished;
        if (frame >= 260) ++republished_late;
      }
      mesh.render(source.data(), full.data());
      for (int t = 0; t < tiles * tiles; ++t) {
        const auto &shown = buffers[t][back[t] ^ 1];
        for (int y = 0; y < tile; ++y)
          for (int x = 0; x < tile; ++x)
            assert(same(shown[y * tile + x], full[((t / tiles) * tile + y) * side + (t % tiles) * tile + x]));
      }
    }
    assert(republished > 0 && republished_late == 0); // once still, nothing is republished
  }

  // The firmware shares each frame's drawing between two cores: one plan, then
  // both take cell rows from one counter. Same bytes as a single-threaded render.
  {
    std::vector<Rgb> split[2] = {std::vector<Rgb>(side * side), std::vector<Rgb>(side * side)};
    std::vector<Rgb> single[2] = {std::vector<Rgb>(side * side), std::vector<Rgb>(side * side)};
    static warp::Mesh reference;
    mesh.configure(side, side, looks[3].rig);
    reference.configure(side, side, looks[3].rig);
    for (int frame = 0; frame < 90; ++frame) {
      const warp::Motion *motion = drive.at(frame, frame >= 20 && frame < 35 ? 15.0f : 6.0f);
      mesh.pose(motion);
      reference.pose(motion);
      const int slot = frame % 2;
      mesh.plan(slot);
      std::atomic<int> next_row{0};
      int helper_rows = 0;
      std::thread helper([&] { helper_rows = mesh.draw_planned_shared(source.data(), split[slot].data(), next_row); });
      const int own_rows = mesh.draw_planned_shared(source.data(), split[slot].data(), next_row);
      helper.join();
      assert(own_rows + helper_rows == n - 1); // every row drawn exactly once
      reference.render(source.data(), single[slot].data(), slot);
      for (int i = 0; i < side * side; ++i) assert(same(split[slot][i], single[slot][i]));
    }
  }

  // The firmware's RGB565 path: 565 source, blended in 565, written in the
  // panel's big-endian format.
  {
    std::vector<uint16_t> source565(side * side);
    for (int i = 0; i < side * side; ++i)
      source565[i] = warp::pack565(uint8_t(source[i] >> 16), uint8_t(source[i] >> 8), uint8_t(source[i]));
    std::vector<warp::Rgb565BE> panel(side * side), full(side * side);
    std::vector<Rgb> wide(side * side), wide565(side * side);
    // At rest it is the source exactly, in the panel format and expanded.
    mesh.configure(side, side, looks[26].rig);
    mesh.pose(drive.still());
    mesh.render(source565.data(), panel.data());
    mesh.render(source565.data(), wide565.data());
    for (int i = 0; i < side * side; ++i) {
      const uint16_t c = source565[i];
      assert(panel[i].v == uint16_t(c << 8 | c >> 8));
      Rgb expanded;
      warp::store565(expanded, c);
      assert(same(wide565[i], expanded));
    }
    // In motion it agrees with the 8-bit path to within one 565 step per channel.
    mesh.pose(drive.at(7, 14));
    mesh.render(source.data(), wide.data());
    mesh.render(source565.data(), wide565.data());
    int differing = 0, worst[3] = {0, 0, 0};
    for (int i = 0; i < side * side; ++i) {
      worst[0] = std::max(worst[0], abs(wide[i].r - wide565[i].r));
      worst[1] = std::max(worst[1], abs(wide[i].g - wide565[i].g));
      worst[2] = std::max(worst[2], abs(wide[i].b - wide565[i].b));
      differing += wide[i].r != wide565[i].r;
    }
    // 565 truncation (< 8 on 5-bit channels, < 4 on green) plus the 1/32 px weight
    // across this pattern's 250-level cliffs (<= 7 per axis; blue's cliff is
    // diagonal, so both weights add); on real art only the first.
    assert(worst[0] <= 16 && worst[1] <= 12 && worst[2] <= 24);
    assert(differing > 0); // it really is a different path
    // The whole direct path: plan, two threads sharing rows, into alternating
    // buffers, equals a full redraw of the same mesh every frame.
    std::vector<warp::Rgb565BE> buffers[2] = {std::vector<warp::Rgb565BE>(side * side),
                                              std::vector<warp::Rgb565BE>(side * side)};
    mesh.configure(side, side, looks[23].rig);
    static Point saved[warp::kMaxVertices];
    for (int frame = 0; frame < 60; ++frame) {
      mesh.pose(drive.at(frame, frame >= 15 && frame < 30 ? 15.0f : 5.0f));
      const int slot = frame % 2;
      mesh.plan(slot);
      std::atomic<int> next_row{0};
      std::thread helper([&] { mesh.draw_planned_shared(source565.data(), buffers[slot].data(), next_row); });
      mesh.draw_planned_shared(source565.data(), buffers[slot].data(), next_row);
      helper.join();
      for (int i = 0; i < n * n; ++i) saved[i] = mesh.rendered[slot][i];
      mesh.render(source565.data(), full.data()); // untracked full redraw of the same mesh
      for (int i = 0; i < n * n; ++i) mesh.rendered[slot][i] = saved[i];
      for (int i = 0; i < side * side; ++i) assert(buffers[slot][i].v == full[i].v);
    }
  }
}

// Nothing above depends on the portrait's size or on mascot geometry.
void generic_tests() {
  static warp::Mesh mesh;
  Driver drive;
  // Sizes and rigs the firmware refuses leave a still, empty mesh.
  warp::Rig rig;
  assert(!mesh.configure(100, 64, rig) && mesh.rig.region_count == 0 && mesh.active_count == 0);
  assert(!mesh.configure(360, 64, rig) && !mesh.configure(0, 8, rig));
  rig.region_count = 1;
  rig.regions[0].ellipse_count = 0; // a patch needs its ellipse
  assert(!mesh.configure(64, 64, rig));
  rig.regions[0].ellipse_count = 1;
  rig.regions[0].under = 0; // only earlier regions
  assert(!mesh.configure(64, 64, rig));
  rig.regions[0].under = -1;
  rig.regions[0].pins = 1; // no such pin
  assert(!mesh.configure(64, 64, rig));

  // A small, wide image with one of each feature: a patch anchored at its top, a
  // blob yielding to it and keeping clear of it, a box pin and a profile pin.
  constexpr int w = 96, h = 48;
  rig = warp::Rig{};
  rig.edge = 8;
  rig.pin_count = 2;
  rig.pins[0].left = 0;
  rig.pins[0].top = 0;
  rig.pins[0].right = 20;
  rig.pins[0].bottom = 20;
  rig.pins[1].rows = 2;
  rig.pins[1].row[0] = {30, 80, 90};
  rig.pins[1].row[1] = {40, 70, 96};
  rig.region_count = 2;
  warp::Region &patch = rig.regions[0];
  patch.kind = warp::Region::Patch;
  patch.ellipse_count = 1;
  patch.ellipses[0] = {40, 24, 10, 10};
  patch.group = 1;
  patch.anchor = 0.3f;
  patch.anchor_top = -1.1f;
  patch.anchor_bottom = 0.1f;
  patch.pins = 3;
  patch.feather = 6;
  patch.squash = 0.07f;
  warp::Region &blob = rig.regions[1];
  blob.kind = warp::Region::Blob;
  blob.ellipse_count = 2;
  blob.ellipses[0] = {72, 20, 16, 14};
  blob.ellipses[1] = {60, 36, 10, 8};
  blob.under = 0;
  blob.pins = 3;
  blob.feather = 4;
  blob.clear = 1;
  blob.travel_x = 6;
  blob.travel_y = 4;
  assert(mesh.configure(w, h, rig));
  assert(mesh.columns == w / 8 + 1 && mesh.rows == h / 8 + 1 && mesh.active_count > 0);
  std::vector<uint16_t> source(w * h);
  for (int i = 0; i < w * h; ++i) source[i] = uint16_t(i * 2654435761u >> 16);
  std::vector<warp::Rgb565BE> out(w * h + 2), full(w * h);
  out.front().v = 0x1234;
  out.back().v = 0x5678;
  mesh.pose(drive.still());
  mesh.render(source.data(), out.data() + 1);
  for (int i = 0; i < w * h; ++i) assert(out[i + 1].v == uint16_t(source[i] << 8 | source[i] >> 8));
  for (int frame = 0; frame < 400; ++frame) {
    mesh.pose(frame % 2 ? drive.wild(80) : drive.at(frame, 20));
    assert_fold_free(mesh);
    // The border and the pins never move.
    for (int i = 0; i < mesh.columns * mesh.rows; ++i) {
      const int col = i % mesh.columns, row = i / mesh.columns;
      const float x = float(col * 8), y = float(row * 8);
      const bool pinned = warp::Mesh::outside(x, y, rig.pins[0]) <= 0 || warp::Mesh::outside(x, y, rig.pins[1]) <= 0;
      if (col == 0 || row == 0 || col == mesh.columns - 1 || row == mesh.rows - 1 || pinned)
        assert(mesh.points[i].x == col * 8 * 256 && mesh.points[i].y == row * 8 * 256);
    }
    mesh.render(source.data(), out.data() + 1, frame % 2);
    mesh.render(source.data(), full.data());
  }
  assert(out.front().v == 0x1234 && out.back().v == 0x5678);
  // Blob weights stay out of the patch's clearance ring and yield where both reach.
  for (int i = 0; i < mesh.columns * mesh.rows; ++i) {
    const float x = float(i % mesh.columns * 8), y = float(i / mesh.columns * 8);
    if (warp::Mesh::radius(x, y, patch.ellipses[0]) < warp::kReach) assert(mesh.weights[i][1] == 0);
    assert(mesh.weights[i][0] + mesh.weights[i][1] <= 1.0001f);
  }
}
} // namespace

int main(int argc, char **argv) {
  const std::vector<Look> looks = read_fixture(argc > 1 ? argv[1] : WARP_FIXTURE);
  assert(looks.size() == 27);
  for (const Look &look : looks) assert(warp::Mesh::valid_rig(look.rig));
  generic_tests();
  mesh_tests(looks);
  puts("Warp layer: rest identity, fold-free mesh and fixed border on all 27 mascot rigs and a generic rig, "
       "pins, clearance, incremental/tile/split/565 equivalence passed.");
}
