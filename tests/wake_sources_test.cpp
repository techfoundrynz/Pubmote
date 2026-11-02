#include <array>
#include <cassert>
#include <cstdint>

using gpio_num_t = int;
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_INVALID_ARG = -1, INPUT_PIN_DISABLED = -1;
constexpr int GPIO_MODE_INPUT = 1, GPIO_PULLUP_ENABLE = 1, GPIO_PULLDOWN_DISABLE = 0, GPIO_INTR_DISABLE = 0;
constexpr int pmu_gpio = 14;
enum esp_sleep_ext1_wakeup_mode_t {
  ESP_EXT1_WAKEUP_ANY_LOW,
  ESP_EXT1_WAKEUP_ANY_HIGH
};
struct InputPinSettings {
  int btn1_gpio = 3, btn1_active_level = 0;
};
struct gpio_config_t {
  uint64_t pin_bit_mask;
  int mode, pull_up_en, pull_down_en, intr_type;
};
#define BIT64(pin) (uint64_t(1) << (pin))
#define ESP_ERROR_CHECK(value) assert((value) == ESP_OK)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)

static InputPinSettings pins;
static std::array<bool, 22> pull_up, pull_down, held;
static bool pmu_high = true, pmu_rtc = true;
static int ext0_calls, ext1_calls, digital_configs, sleep_hold_releases;
static uint64_t ext1_mask;
static esp_sleep_ext1_wakeup_mode_t ext1_level;
static InputPinSettings settings_get_pins() {
  return pins;
}
static bool rtc_gpio_is_valid_gpio(int pin) {
  return pin >= 0 && pin <= 21 && (pin != pmu_gpio || pmu_rtc);
}
static int rtc_gpio_pullup_en(int pin) {
  pull_up.at(pin) = true;
  return ESP_OK;
}
static int rtc_gpio_pullup_dis(int pin) {
  pull_up.at(pin) = false;
  return ESP_OK;
}
static int rtc_gpio_pulldown_en(int pin) {
  pull_down.at(pin) = true;
  return ESP_OK;
}
static int rtc_gpio_pulldown_dis(int pin) {
  pull_down.at(pin) = false;
  return ESP_OK;
}
static int gpio_get_level(int pin) {
  assert(pin == pmu_gpio && !held.at(pin));
  return pmu_high;
}
static int esp_sleep_enable_ext0_wakeup(int pin, int level) {
  assert(pin == pmu_gpio && level == 0);
  ++ext0_calls;
  return ESP_OK;
}
static int esp_sleep_enable_ext1_wakeup(uint64_t mask, esp_sleep_ext1_wakeup_mode_t level) {
  ++ext1_calls;
  ext1_mask = mask;
  ext1_level = level;
  return ESP_OK;
}
static int rtc_gpio_hold_dis(int pin) {
  held.at(pin) = false;
  return ESP_OK;
}
static int rtc_gpio_deinit(int pin) {
  assert(!held.at(pin));
  return ESP_OK;
}
static int gpio_config(const gpio_config_t *config) {
  assert(!held.at(pmu_gpio));
  assert(config->pin_bit_mask == BIT64(pmu_gpio) && config->mode == GPIO_MODE_INPUT);
  assert(config->pull_up_en == GPIO_PULLUP_ENABLE && config->pull_down_en == GPIO_PULLDOWN_DISABLE);
  ++digital_configs;
  return ESP_OK;
}
static void gpio_deep_sleep_hold_dis() {
  ++sleep_hold_releases;
}
#include "wake_preinit.inc"
#include "wake_sources.inc"

static void reset() {
  pins = {};
  pull_up.fill(false);
  pull_down.fill(false);
  held.fill(false);
  pmu_high = pmu_rtc = true;
  ext0_calls = ext1_calls = digital_configs = sleep_hold_releases = 0;
  ext1_mask = 0;
}

int main() {
  reset();
  assert(enable_wake() == ESP_OK);
  assert(ext1_calls == 1 && ext0_calls == 0 && ext1_level == ESP_EXT1_WAKEUP_ANY_LOW);
  assert(pull_up.at(3) && !pull_down.at(3));
#ifdef PMU_INT
  assert(ext1_mask == (BIT64(3) | BIT64(pmu_gpio)) && pull_up.at(pmu_gpio));
#else
  assert(ext1_mask == BIT64(3));
#endif

  reset();
  pins.btn1_active_level = 1;
  assert(enable_wake() == ESP_OK);
  assert(ext1_calls == 1 && ext1_mask == BIT64(3) && ext1_level == ESP_EXT1_WAKEUP_ANY_HIGH);
  assert(pull_down.at(3) && !pull_up.at(3));
#ifdef PMU_INT
  assert(ext0_calls == 1 && pull_up.at(pmu_gpio));
#else
  assert(ext0_calls == 0);
#endif

  reset();
  pins.btn1_gpio = INPUT_PIN_DISABLED;
  assert(enable_wake() == ESP_OK && ext0_calls == 0);
#ifdef PMU_INT
  assert(ext1_calls == 1 && ext1_mask == BIT64(pmu_gpio) && ext1_level == ESP_EXT1_WAKEUP_ANY_LOW);
#else
  assert(ext1_calls == 0);
#endif

  reset();
  pmu_high = false;
  assert(enable_wake() == ESP_OK);
  assert(ext1_mask == BIT64(3) && ext0_calls == 0 && !pull_up.at(pmu_gpio));
  reset();
  pmu_rtc = false;
  assert(enable_wake() == ESP_OK && ext1_mask == BIT64(3) && ext0_calls == 0);
  reset();
  pins.btn1_gpio = 22;
  assert(enable_wake() == ESP_ERR_INVALID_ARG && ext1_calls == 0 && ext0_calls == 0);

  reset();
  held.at(pmu_gpio) = true;
  power_management_preinit();
  assert(sleep_hold_releases == 1);
#ifdef PMU_INT
  assert(!held.at(pmu_gpio) && digital_configs == 1);
#else
  assert(held.at(pmu_gpio) && digital_configs == 0);
#endif
}
