#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <vector>
#include "../firmware/src/remote/wake_button_sampler.h"
#define ESP_LOGI(...) ((void)0)
static WakeButtonSample sampled;
static bool suppress_sleep_feedback;
static WakeButtonSample sample_result() { return sampled; }
#define wake_button_sampler_result sample_result
#include "wake_hold.inc"

enum Code { ReadPin, Sub, BranchLow, Move, Jump, Label, Load, Add, Store, End, Halt };
struct Op { Code code; int a = 0, b = 0, c = 0; };
using ulp_insn_t = Op;
constexpr int R0 = 0, R3 = 3, RTC_GPIO_IN_REG = 0, RTC_GPIO_IN_NEXT_S = 10;
constexpr int SAMPLE_RESULT_WORD = 126, SAMPLE_COUNT_WORD = 127, SAMPLE_COUNT = 50;
#define I_RD_REG(reg, lo, hi) Op{ReadPin, lo, hi}
#define I_SUBI(d, s, n) Op{Sub, d, s, n}
#define M_BL(label, value) Op{BranchLow, label, value}
#define I_MOVI(d, value) Op{Move, d, value}
#define M_BX(label) Op{Jump, label}
#define M_LABEL(label) Op{Label, label}
#define I_LD(d, s, offset) Op{Load, d, s, offset}
#define I_ADDI(d, s, value) Op{Add, d, s, value}
#define I_ST(s, d, offset) Op{Store, s, d, offset}
#define I_END() Op{End}
#define I_HALT() Op{Halt}
static std::vector<Op> program(int gpio, int active_level) {
#include "wake_ulp_program.inc"
  return {program, program + sizeof(program) / sizeof(program[0])};
}
struct Outcome { WakeButtonSample result; int samples; int ticks; };
static Outcome run(int gpio, int active_level, int release_tick, int repress_tick = 1000) {
  const auto ops = program(gpio, active_level);
  std::map<int, int> labels;
  for (int i = 0; i < (int)ops.size(); ++i)
    if (ops[i].code == Label) labels[ops[i].a] = i;
  std::array<uint32_t, 128> mem{};
  mem[SAMPLE_RESULT_WORD] = WAKE_SAMPLE_PENDING;
  bool timer = true;
  int ticks = 0;
  for (; timer && ticks < 100; ) {
    ++ticks;
    // FSM registers need not survive HALT; only RTC memory is retained.
    std::array<uint16_t, 4> reg{0xffff, 0xffff, 0xffff, 0xffff};
    bool held = ticks < release_tick || ticks >= repress_tick;
    int pc = 0, steps = 0;
    bool halted = false;
    while (!halted && ++steps < 100) {
      assert(pc >= 0 && pc < (int)ops.size());
      const auto op = ops[pc++];
      switch (op.code) {
      case ReadPin:
        assert(op.a == gpio + 10 && op.b == op.a && op.b <= 31);
        reg[0] = held ? active_level : !active_level; break;
      case Sub: reg[op.a] = uint16_t(reg[op.b] - op.c); break;
      case BranchLow: if (reg[0] < op.b) pc = labels.at(op.a); break;
      case Move: reg[op.a] = uint16_t(op.b); break;
      case Jump: pc = labels.at(op.a); break;
      case Label: break;
      case Load: reg[op.a] = uint16_t(mem.at(reg[op.b] + op.c)); break;
      case Add: reg[op.a] = uint16_t(reg[op.b] + op.c); break;
      case Store: {
        const int address = reg[op.b] + op.c;
        assert(address == SAMPLE_RESULT_WORD || address == SAMPLE_COUNT_WORD);
        mem.at(address) = 0xabcd0000U | reg[op.a]; // Hardware ST adds upper metadata.
        break;
      }
      case End: timer = false; break;
      case Halt: halted = true; break;
      }
    }
    assert(halted);
  }
  assert(!timer); // Every verdict stops subsequent ULP invocations.
  return {WakeButtonSample(mem[SAMPLE_RESULT_WORD] & 0xffff), int(mem[SAMPLE_COUNT_WORD] & 0xffff), ticks};
}
int main() {
  for (int pin : {0, 3, 15, 21}) {
    for (int level : {0, 1}) {
      auto held = run(pin, level, 60);
      assert(held.result == WAKE_SAMPLE_CONFIRMED && held.samples == 50 && held.ticks == 50);
      auto short_tap = run(pin, level, 10);
      assert(short_tap.result == WAKE_SAMPLE_CANCELLED && short_tap.samples == 9 && short_tap.ticks == 10);
      auto early = run(pin, level, 49);
      assert(early.result == WAKE_SAMPLE_CANCELLED && early.ticks == 49);
      auto repress = run(pin, level, 10, 15);
      assert(repress.result == WAKE_SAMPLE_CANCELLED && repress.ticks == 10);
      auto released = run(pin, level, 1);
      assert(released.result == WAKE_SAMPLE_CANCELLED && released.samples == 0);
    }
  }
  // Main CPU uses the recorded verdict, never the later GPIO level or boot age.
  for (auto result : {WAKE_SAMPLE_UNAVAILABLE, WAKE_SAMPLE_PENDING, WAKE_SAMPLE_CANCELLED, WAKE_SAMPLE_CONFIRMED}) {
    sampled = result;
    suppress_sleep_feedback = false;
    assert(check_button_press() == (result == WAKE_SAMPLE_CONFIRMED));
    assert(suppress_sleep_feedback == (result != WAKE_SAMPLE_CONFIRMED));
  }
}
