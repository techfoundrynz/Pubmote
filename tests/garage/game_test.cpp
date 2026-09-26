#include "../../firmware/src/games/garage_game.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace garage;
static void tick(Game &g, int frames = 1) {
  for (int i = 0; i < frames; ++i) g.advance(1.0f / 60);
}
static void collision_tests() {
  Game g;
  g.reset();
  assert(g.state == State::Ready && g.lives == 3);
  tick(g, 60);
  assert(g.x == 26);
  g.action(); tick(g, 180);
  assert(g.state == State::Playing && g.x <= 56.01f && g.grounded);
  g.action();
  bool landed_on_box = false;
  for (int i = 0; i < 90; ++i) {
    tick(g);
    landed_on_box |= g.grounded && std::abs(g.y - 62) < 0.01f;
  }
  assert(landed_on_box);
  g.reset(); g.action(); g.steer(0);
  g.action(); tick(g, 4);
  const float first_vy = g.vy;
  g.action(); tick(g);
  assert(g.vy > first_vy); // No second jump in midair.
  tick(g, 100);
  assert(g.grounded && std::abs(g.y - 74) < 0.01f);
  g.steer(-1); tick(g, 180);
  assert(g.x >= Game::player_width / 2);
  g.reset(); g.action(); g.steer(0);
  g.x = 154; g.y = 74; tick(g, 100);
  assert(g.state == State::Crashed && g.lives == 2);
  g.action();
  assert(g.state == State::Playing && g.x == 26);
  for (int life = 0; life < 2; ++life) {
    g.steer(0); g.x = 154; g.y = 74; tick(g, 100);
    if (life == 0) g.action();
  }
  assert(g.state == State::Over && g.lives == 0);
  g.action();
  assert(g.lives == 3 && g.level == 0 && g.score == 0);
  g.reset(); g.action(); g.x = g.checkpoint_x; g.steer(0); tick(g);
  assert(g.checkpoint);
  g.x = 274; g.y = 90; tick(g, 100);
  assert(g.state == State::Crashed);
  g.action();
  assert(g.x == g.checkpoint_x + 6 && g.checkpoint);
  g.reset(); g.action(); g.steer(0);
  for (size_t i = 0; i < g.count; ++i) {
    auto &o = g.objects[i];
    if (o.kind != Kind::Battery) continue;
    g.x = o.x + 2; g.y = o.y + 5; g.vy = 0; tick(g);
    assert(!o.active && g.score == 10 && g.batteries == 1);
    tick(g); assert(g.score == 10); break;
  }
  g.reset(); g.action(); g.steer(0); g.invulnerable = 0;
  for (auto &o : g.objects) {
    if (o.kind != Kind::Drone || o.home != 335) continue;
    g.x = o.x + 4; g.y = o.y - 0.1f; g.vy = 20; g.grounded = false;
    tick(g);
    assert(!o.active && (g.events & Game::Stomp) && g.vy < 0); break;
  }
  g.reset(); g.action(); g.steer(0); g.invulnerable = 0;
  g.x = 212; tick(g);
  assert(g.state == State::Crashed);
  g.reset(); g.action();
  g.advance(std::numeric_limits<float>::quiet_NaN());
  assert(g.x == 26);
  g.advance(10);
  assert(g.x < 28);
}
// Terrain remains traversable using the brake at high pace. Combat and power-ups
// are tested separately below; this controller does not plan enemy encounters.
static void terrain_playthrough(bool maximum_pace = false) {
  Game g;
  g.reset(); g.action();
  if (maximum_pace) g.ride_seconds = 120;
  for (int level = 0; level < Game::level_count; ++level) {
    for (size_t i = 0; i < g.count; ++i)
      if (g.objects[i].kind == Kind::Drone || g.objects[i].kind >= Kind::Shield) g.objects[i].active = false;
    for (int frame = 0; frame < 6000 && g.state == State::Playing; ++frame) {
      g.steer(g.vx > 38 ? 0 : 1);
      if (g.grounded) {
        bool jump = false;
        for (size_t i = 0; i < g.count; ++i) {
          const auto &o = g.objects[i];
          if (!o.active) continue;
          if ((o.kind == Kind::Platform || o.kind == Kind::Drone) &&
              o.x > g.x && o.x - g.x < (o.kind == Kind::Drone ? 20 : 15) && o.y < g.y && o.y + o.h > g.y - 14)
            jump = true;
          if (o.kind == Kind::Ground &&
              std::abs(o.y - g.y) < 0.01f && g.x >= o.x &&
              o.x + o.w - g.x > 0 && o.x + o.w - g.x < 6)
            jump = true;
        }
        if (jump) g.action();
      }
      tick(g);
    }
    if (g.state != State::Cleared && g.state != State::Won) {
      std::printf("Unfinished level %d state %d at %.2f,%.2f lives %d\n",
                  level + 1, int(g.state), g.x, g.y, g.lives);
      std::fflush(stdout);
    }
    assert(g.state == (level == Game::level_count - 1 ? State::Won : State::Cleared));
    assert(g.lives >= 3 && g.stage_crashes == 0 && g.checkpoint);
    if (level < Game::level_count - 1) g.action();
  }
  assert(g.score >= 300 && g.batteries > 0);
  g.action();
  assert(g.state == State::Playing && g.level == 0 && g.score >= 300 && g.lap == 1);
}
static void timing_and_difficulty_tests() {
  Game g;
  g.reset();
  tick(g, 600);
  assert(g.ride_seconds == 0 && g.riding_speed() == 40);
  g.action(); g.steer(0); g.action();
  float minimum_y = 74;
  int frames = 0;
  do {
    g.advance(1.0f / 120);
    minimum_y = std::min(minimum_y, g.y);
    ++frames;
  } while (!g.grounded && frames < 240);
  const float airtime = frames / 120.0f;
  const float height = 74 - minimum_y;
  assert(airtime > 0.7f && airtime < 0.82f);
  assert(height > 21 && height < 23);
  std::printf("Jump: %.3f seconds, %.2f units high\n", airtime, height);
  g.reset(); g.action(); g.steer(0);
  tick(g, 600);
  assert(g.riding_speed() > 44.4f && g.riding_speed() < 44.6f);
  tick(g, 3600);
  assert(g.riding_speed() == 60);
  g.level = 2;
  assert(g.riding_speed() == 64);
  g.state = State::Crashed;
  const float elapsed = g.ride_seconds;
  tick(g, 600);
  assert(g.ride_seconds == elapsed);
  g.action();
  assert(g.ride_seconds == elapsed);
  g.reset();
  assert(g.ride_seconds == 0 && g.riding_speed() == 40);
}

static void progression_tests() {
  Game g;
  g.reset(); g.action(); g.steer(0);
  float previous_finish = 0;
  for (int stage = 0; stage < Game::level_count; ++stage) {
    assert(g.finish_x > previous_finish && g.count <= Game::capacity);
    previous_finish = g.finish_x;
    const int starting_lives = g.lives;
    int collected = 0;
    // Isolate reward behavior from terrain traversal (covered by playthrough).
    for (size_t i = 0; i < g.count; ++i) {
      auto &o = g.objects[i];
      if (o.kind != Kind::Battery) continue;
      g.x = o.x + 2; g.y = o.y + 5; g.vx = g.vy = 0;
      tick(g);
      ++collected;
      assert(!o.active && g.stage_batteries == collected);
      assert(g.lives == std::min(3, starting_lives + (collected >= Game::battery_goal ? 1 : 0)));
      assert(g.combo == std::min(collected, 4));
      if (collected == 5 && starting_lives < 3) assert(g.events & Game::ExtraLife);
    }
    assert(collected >= Game::battery_goal);
    g.x = g.checkpoint_x + 6; g.y = 74; g.vy = 0;
    tick(g, 180);
    assert(g.combo == 0);
    g.x = g.finish_x + 4; g.y = 74; g.vy = 0;
    tick(g);
    assert(g.stars == 3 && g.total_stars == (stage + 1) * 3);
    const int finished_score = g.score;
    tick(g, 60);
    assert(g.score == finished_score);
    if (stage < 2) {
      g.action(); g.steer(0);
      assert(g.stage_batteries == 0 && g.stars == 0);
    }
  }
  assert(g.state == State::Won && g.total_stars == 9);
  g.action();
  assert(g.total_stars == 0 && g.lives == 3 && g.lap == 1);
  g.stage_crashes = 1; g.x = g.finish_x + 4; g.y = 74;
  tick(g);
  assert(g.stars == 1 && g.total_stars == 1);
}

static void powerup_and_lap_tests() {
  auto arena = [] {
    Game g; g.reset(); g.action(); g.steer(0); g.invulnerable = 0;
    g.count = 1; g.objects[0] = {0, 74, 100, 26, Kind::Ground};
    return g;
  };
  for (Kind kind : {Kind::Shield, Kind::Magnet, Kind::Turbo}) {
    Game g = arena();
    const float normal_speed = g.riding_speed();
    g.objects[g.count++] = {24, 62, 6, 8, kind};
    tick(g);
    assert(!g.objects[1].active && (g.events & Game::Powerup));
    if (kind == Kind::Magnet) {
      g.objects[g.count++] = {44, 50, 5, 7, Kind::Battery};
      tick(g);
      assert(!g.objects[2].active && g.batteries == 1);
    } else {
      if (kind == Kind::Turbo) assert(g.riding_speed() > normal_speed + 7);
      g.objects[g.count++] = {24, 67, 8, 7, Kind::Drone, true, 24, 1};
      tick(g);
      assert(!g.objects[2].active && g.lives == 3 && (g.events & Game::Stomp));
    }
    g.state = State::Cleared;
    const float timer = g.shield + g.magnet + g.turbo;
    tick(g, 600);
    assert(g.shield + g.magnet + g.turbo == timer);
    g.state = State::Playing;
    tick(g, 600);
    assert(g.shield == 0 && g.magnet == 0 && g.turbo == 0);
    g.objects[g.count++] = {24, 67, 8, 7, Kind::Drone, true, 24, 1};
    tick(g);
    assert(g.state == State::Crashed && g.lives == 2);
    g.action();
    assert(g.shield == 0 && g.magnet == 0 && g.turbo == 0 && !g.objects[1].active);
  }
  Game pit = arena();
  pit.shield = pit.turbo = 10; pit.x = 120; pit.y = 90;
  tick(pit, 100);
  assert(pit.state == State::Crashed && pit.lives == 2);
  Game g = arena();
  g.lives = 2; g.stage_batteries = 4;
  g.objects[g.count++] = {24, 62, 5, 7, Kind::Battery};
  tick(g);
  assert(g.lives == 3 && (g.events & Game::ExtraLife));
  tick(g); assert(g.lives == 3);
  g.reset(); g.action(); g.lives = 2; g.score = 123; g.ride_seconds = 12;
  int previous_enemies = 0;
  float previous_speed = 0;
  for (int lap = 0; lap <= 10; ++lap) {
    int enemies = 0;
    for (size_t i = 0; i < g.count; ++i) if (g.objects[i].kind == Kind::Drone) ++enemies;
    assert(g.count <= Game::capacity && enemies >= previous_enemies);
    if (lap > 0 && lap <= 5) assert(enemies > previous_enemies);
    assert(g.riding_speed() >= previous_speed && g.riding_speed() <= 76);
    std::printf("Lap %d: %d patrols, speed %.1f\n", lap + 1, enemies, g.riding_speed());
    previous_enemies = enemies; previous_speed = g.riding_speed();
    Game stages = g;
    for (int stage = 0; stage < Game::level_count; ++stage) {
      assert(stages.count <= Game::capacity);
      for (size_t i = 0; i < stages.count; ++i) {
        const auto &o = stages.objects[i];
        if (o.kind != Kind::Drone || o.y != 67) continue;
        bool supported = false;
        for (size_t j = 0; j < stages.count; ++j) {
          const auto &floor = stages.objects[j];
          supported |= floor.kind == Kind::Ground && o.home - 7 >= floor.x && o.home + 15 <= floor.x + floor.w;
        }
        assert(supported);
      }
      if (stage < 2) { stages.state = State::Cleared; stages.action(); }
    }
    g.state = State::Won; g.level = 2; g.action();
    assert(g.lap == lap + 1 && g.level == 0 && g.score == 123 && g.lives == 2 && g.ride_seconds == 12);
  }
  g.state = State::Over; g.action();
  assert(g.lap == 0 && g.score == 0 && g.lives == 3 && g.ride_seconds == 0);
}

int main() {
  collision_tests(); powerup_and_lap_tests(); timing_and_difficulty_tests(); progression_tests(); terrain_playthrough(); terrain_playthrough(true);
  std::puts("Garage Rider: collisions, pickups, respawn, controls, power-ups, harder laps and terrain traversal passed");
}
