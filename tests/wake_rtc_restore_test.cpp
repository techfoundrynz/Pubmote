#include <cassert>
#include <cstdint>
#include <map>

#define RTC_IRAM_ATTR
constexpr uint32_t SENS_SAR_PERI_CLK_GATE_CONF_REG = 1, SENS_IOMUX_CLK_EN = 1U << 31;
constexpr uint32_t RTC_GPIO_ENABLE_W1TC_REG = 2, RTC_GPIO_ENABLE_W1TC_S = 10, RTC_CNTL_PAD_HOLD_REG = 3;
static uint32_t sampler_pad_reg, sampler_pad_config, sampler_pin_mask, sampler_hold_mask;
static std::map<uint32_t, uint32_t> registers;
static uint32_t output_enabled;
static int step;

static void set_mask(uint32_t reg, uint32_t mask) {
  assert(step++ == 0 && reg == SENS_SAR_PERI_CLK_GATE_CONF_REG && mask == SENS_IOMUX_CLK_EN);
  registers[reg] |= mask;
}
static void write_reg(uint32_t reg, uint32_t value) {
  if (reg == sampler_pad_reg) {
    assert(step++ == 1 && value == sampler_pad_config);
    registers[reg] = value;
  }
  else {
    assert(step++ == 2 && reg == RTC_GPIO_ENABLE_W1TC_REG);
    output_enabled &= ~value;
  }
}
static void clear_mask(uint32_t reg, uint32_t mask) {
  assert(step++ == 3 && reg == RTC_CNTL_PAD_HOLD_REG && mask == sampler_hold_mask);
  assert(registers[SENS_SAR_PERI_CLK_GATE_CONF_REG] & SENS_IOMUX_CLK_EN);
  assert(registers[sampler_pad_reg] == sampler_pad_config);
  assert(!(output_enabled & (sampler_pin_mask << RTC_GPIO_ENABLE_W1TC_S)));
  registers[reg] &= ~mask;
}
#define SET_PERI_REG_MASK(reg, mask) set_mask(reg, mask)
#define REG_WRITE(reg, value) write_reg(reg, value)
#define CLEAR_PERI_REG_MASK(reg, mask) clear_mask(reg, mask)
#include "wake_rtc_restore.inc"

int main() {
  for (int pin : {0, 3, 15, 21}) {
    for (uint32_t pull : {1U << 27, 1U << 28}) {
      registers.clear();
      step = 0;
      sampler_pin_mask = sampler_hold_mask = 1U << pin;
      sampler_pad_reg = 100 + pin;
      sampler_pad_config = pull | (1U << 19) | (1U << 13);
      registers[SENS_SAR_PERI_CLK_GATE_CONF_REG] = 0x40;
      registers[RTC_CNTL_PAD_HOLD_REG] = sampler_hold_mask | (1U << 14);
      output_enabled = UINT32_MAX;
      restore_wake_button_input();
      assert(step == 4);
      assert(registers[SENS_SAR_PERI_CLK_GATE_CONF_REG] == (0x40 | SENS_IOMUX_CLK_EN));
      assert(registers[RTC_CNTL_PAD_HOLD_REG] == (1U << 14));
      assert(output_enabled == (UINT32_MAX & ~(sampler_pin_mask << RTC_GPIO_ENABLE_W1TC_S)));
    }
  }
}
