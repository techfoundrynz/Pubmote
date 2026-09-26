#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace garage {

enum class State { Ready, Playing, Crashed, Cleared, Won, Over };
enum class Kind { Ground, Platform, Battery, Drone, Garage, Checkpoint, Shield, Magnet, Turbo };

struct Object {
  float x = 0, y = 0, w = 0, h = 0;
  Kind kind = Kind::Ground;
  bool active = true;
  float home = 0;
  int direction = 1;
};

// World coordinates use a 100-unit-wide viewport. Player y is the tire's bottom.
class Game {
public:
  static constexpr float player_width = 8;
  static constexpr float player_height = 14;
  float finish_x = 360;
  float checkpoint_x = 180;
  static constexpr int battery_goal = 5;
  static constexpr int level_count = 3;
  static constexpr size_t capacity = 48;
  State state = State::Ready;
  std::array<Object, capacity> objects{};
  size_t count = 0;
  float x = 26, y = 74, vx = 0, vy = 0, camera = 0;
  float invulnerable = 0;
  float ride_seconds = 0;
  int lap = 0;
  float shield = 0, magnet = 0, turbo = 0;
  float pace_bonus() const { return std::min(ride_seconds * 0.45f, 20.0f); }
  float riding_speed() const { return std::min(40.0f + level * 2.0f + std::min(lap, 6) * 6.0f + pace_bonus(), 76.0f) + (turbo > 0 ? 8.0f : 0.0f); }
  float enemy_speed() const { return std::min(12.0f + level * 2 + std::min(lap, 10) * 1.5f + pace_bonus() / 3, 36.0f); }
  int level = 0, lives = 3, score = 0, batteries = 0, direction = 1;
  int stage_batteries = 0, stage_crashes = 0, stars = 0, total_stars = 0;
  int combo = 0;
  float combo_time = 0;
  bool grounded = true, checkpoint = false;
  unsigned events = 0;
  enum Event { Jump = 1, Pickup = 2, Stomp = 4, Crash = 8, Finish = 16, Check = 32, ExtraLife = 64, Powerup = 128 };

  void reset() {
    level = lap = 0;
    lives = 3;
    score = batteries = 0;
    ride_seconds = 0;
    total_stars = 0;
    load_level();
    state = State::Ready;
  }

  void action() {
    events = 0;
    if (state == State::Ready) {
      state = State::Playing;
    } else if (state == State::Crashed) {
      respawn();
      state = State::Playing;
    } else if (state == State::Cleared) {
      ++level;
      load_level();
      state = State::Playing;
    } else if (state == State::Won) {
      ++lap;
      level = 0;
      total_stars = 0;
      load_level();
      state = State::Playing;
    } else if (state == State::Over) {
      reset();
      state = State::Playing;
    } else {
      jump_buffer = 0.12f;
    }
  }

  void steer(int value) {
    direction = value < 0 ? -1 : (value > 0 ? 1 : 0);
  }

  void advance(float seconds) {
    events = 0;
    if (state != State::Playing || !std::isfinite(seconds) || seconds <= 0) return;
    // A stalled frame slows the game instead of tunneling through a platform.
    accumulator += std::min(seconds, 0.1f);
    constexpr float dt = 1.0f / 120.0f;
    while (accumulator >= dt && state == State::Playing) {
      step(dt);
      accumulator -= dt;
    }
  }

private:
  float accumulator = 0, jump_buffer = 0, coyote = 0;

  void add(float px, float py, float w, float h, Kind kind) {
    objects[count++] = {px, py, w, h, kind, true, px, 1};
  }

  void load_level() {
    count = 0;
    checkpoint = false;
    stage_batteries = stage_crashes = stars = combo = 0;
    combo_time = 0;
    // Three deliberately different routes: learn the hops, climb the stacks,
    // then cross wider gaps among more patrols. Each has a safe checkpoint.
    finish_x = level == 0 ? 360.0f : level == 1 ? 460.0f : 560.0f;
    checkpoint_x = level == 0 ? 180.0f : level == 1 ? 350.0f : 320.0f;
    const float gap = 16.0f + level * 4.0f;
    const std::array<float, 3> edges = level == 0
        ? std::array<float, 3>{146, 266, 0}
        : level == 1 ? std::array<float, 3>{146, 266, 386}
                     : std::array<float, 3>{146, 286, 406};
    float left = 0;
    for (float edge : edges) {
      if (edge == 0) continue;
      add(left, 74, edge - left, 26, Kind::Ground);
      add(edge - 14, 62, 5, 7, Kind::Battery);
      left = edge + gap;
    }
    add(left, 74, finish_x + 50 - left, 26, Kind::Ground);
    const std::array<float, 4> crates = level == 0
        ? std::array<float, 4>{60, 230, 0, 0}
        : level == 1 ? std::array<float, 4>{60, 180, 290, 0}
                     : std::array<float, 4>{60, 180, 465, 0};
    for (float base : crates) {
      if (base == 0) continue;
      add(base, 62, 18, 12, Kind::Platform);
      add(base + 5, 53, 5, 7, Kind::Battery);
      // The middle trail emphasizes two-tier rooftop routes.
      if (level == 1 || base == 60) {
        add(base + 24, 49, 19, 4, Kind::Platform);
        add(base + 30, 40, 5, 7, Kind::Battery);
      }
    }
    add(finish_x - 22, 62, 5, 7, Kind::Battery);
    if (level == 0) {
      add(210, 67, 8, 7, Kind::Drone);
      add(335, 67, 8, 7, Kind::Drone);
    } else if (level == 1) {
      add(230, 67, 8, 7, Kind::Drone);
      add(435, 67, 8, 7, Kind::Drone);
    } else {
      add(240, 67, 8, 7, Kind::Drone);
      add(350, 67, 8, 7, Kind::Drone);
      add(530, 67, 8, 7, Kind::Drone);
    }
    // Add patrols on open ground, away from pits, crates and respawn points.
    // Later laps fill additional slots; storage and update work remain bounded.
    int remaining = 2 + std::min(lap, 5);
    for (float px = 108; px < finish_x - 18 && remaining > 0; px += 24) {
      bool floor = false, clear = std::abs(px - checkpoint_x - 6) > 22;
      for (size_t i = 0; i < count; ++i) {
        const auto &o = objects[i];
        if (o.kind == Kind::Ground && px - 10 >= o.x && px + 18 <= o.x + o.w) floor = true;
        if ((o.kind == Kind::Platform || o.kind == Kind::Drone) &&
            px + 18 > o.x && px - 14 < o.x + o.w) clear = false;
      }
      if (floor && clear) { add(px, 67, 8, 7, Kind::Drone); --remaining; }
    }
    // Aerial patrols guard upper routes on later laps, leaving the ground route open.
    for (int i = 0; i < std::min(lap, 5); ++i)
      add(96.0f + i * 48, 32, 8, 7, Kind::Drone);
    add(42, 44, 6, 8, Kind::Shield);
    add(90, 39, 6, 8, Kind::Magnet);
    add(checkpoint_x + 14, 48, 6, 8, Kind::Turbo);
    add(checkpoint_x, 54, 3, 20, Kind::Checkpoint);
    add(finish_x, 48, 28, 26, Kind::Garage);
    respawn();
  }

  void respawn() {
    x = checkpoint ? checkpoint_x + 6 : 26.0f;
    y = 74;
    vx = vy = 0;
    grounded = true;
    direction = 1;
    invulnerable = 1.5f;
    shield = magnet = turbo = 0;
    accumulator = jump_buffer = coyote = 0;
    camera = std::max(0.0f, std::min(x - 30, finish_x - 65));
  }

  bool overlaps(const Object &o) const {
    return x + player_width / 2 > o.x && x - player_width / 2 < o.x + o.w &&
           y > o.y && y - player_height < o.y + o.h;
  }

  static bool solid(const Object &o) {
    return o.kind == Kind::Ground || o.kind == Kind::Platform;
  }

  void crash() {
    ++stage_crashes;
    combo = 0;
    combo_time = 0;
    --lives;
    state = lives > 0 ? State::Crashed : State::Over;
    events |= Crash;
    vx = vy = 0;
  }

  void step(float dt) {
    ride_seconds += dt;
    shield = std::max(0.0f, shield - dt);
    magnet = std::max(0.0f, magnet - dt);
    turbo = std::max(0.0f, turbo - dt);
    combo_time = std::max(0.0f, combo_time - dt);
    if (combo_time == 0) combo = 0;
    invulnerable = std::max(0.0f, invulnerable - dt);
    coyote = grounded ? 0.09f : std::max(0.0f, coyote - dt);
    if (jump_buffer > 0 && coyote > 0) {
      vy = -105;
      grounded = false;
      coyote = jump_buffer = 0;
      events |= Jump;
    }
    jump_buffer = std::max(0.0f, jump_buffer - dt);
    const float target = direction * riding_speed();
    vx += std::max(-150 * dt, std::min(150 * dt, target - vx));
    x += vx * dt;
    x = std::max(player_width / 2, x);
    for (size_t i = 0; i < count; ++i) {
      const auto &o = objects[i];
      if (!solid(o) || !overlaps(o)) continue;
      if (vx > 0) x = o.x - player_width / 2;
      else if (vx < 0) x = o.x + o.w + player_width / 2;
      vx = 0;
    }
    const float old_y = y;
    vy = std::min(vy + (vy < 0 ? 250.0f : 330.0f) * dt, 130.0f);
    y += vy * dt;
    grounded = false;
    for (size_t i = 0; i < count; ++i) {
      const auto &o = objects[i];
      if (!solid(o) || !overlaps(o)) continue;
      if (vy >= 0 && old_y <= o.y + 0.01f) {
        y = o.y;
        vy = 0;
        grounded = true;
      } else if (vy < 0 && old_y - player_height >= o.y + o.h - 0.01f) {
        y = o.y + o.h + player_height;
        vy = 0;
      }
    }
    if (y > 108) { crash(); return; }
    for (size_t i = 0; i < count; ++i) {
      auto &o = objects[i];
      if (!o.active) continue;
      if (o.kind == Kind::Drone) {
        o.x += o.direction * enemy_speed() * dt;
        if (o.x > o.home + 7) { o.x = o.home + 7; o.direction = -1; }
        if (o.x < o.home - 7) { o.x = o.home - 7; o.direction = 1; }
      }
      const bool attracted = o.kind == Kind::Battery && magnet > 0 &&
          std::abs(x - o.x - o.w / 2) < 22 && std::abs(y - player_height / 2 - o.y - o.h / 2) < 24;
      if (!overlaps(o) && !attracted) continue;
      if (o.kind == Kind::Battery) {
        o.active = false;
        combo = std::min(combo + 1, 4);
        combo_time = 2.5f;
        score += 10 * combo;
        ++batteries;
        if (++stage_batteries == battery_goal) {
          if (lives < 3) { ++lives; events |= ExtraLife; }
          else score += 50;
        }
        events |= Pickup;
      } else if (o.kind == Kind::Shield || o.kind == Kind::Magnet || o.kind == Kind::Turbo) {
        o.active = false;
        if (o.kind == Kind::Shield) shield = 4;
        if (o.kind == Kind::Magnet) magnet = 8;
        if (o.kind == Kind::Turbo) turbo = 2.5f;
        events |= Powerup;
      } else if (o.kind == Kind::Drone) {
        if (shield > 0 || turbo > 0) {
          o.active = false;
          score += 25;
          events |= Stomp;
        } else if (vy > 0 && old_y <= o.y + 1) {
          o.active = false;
          y = o.y;
          vy = -70;
          score += 25;
          events |= Stomp;
        } else if (invulnerable <= 0) {
          crash();
          return;
        }
      } else if (o.kind == Kind::Checkpoint && !checkpoint) {
        checkpoint = true;
        events |= Check;
      } else if (o.kind == Kind::Garage) {
        stars = 1 + (stage_batteries >= battery_goal ? 1 : 0) + (stage_crashes == 0 ? 1 : 0);
        total_stars += stars;
        score += 100 + (stars - 1) * 50;
        state = level + 1 == level_count ? State::Won : State::Cleared;
        events |= Finish;
        return;
      }
    }
    camera = std::max(0.0f, std::min(x - 30, finish_x - 65));
  }
};

} // namespace garage
