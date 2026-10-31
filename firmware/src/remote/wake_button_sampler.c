#include "remote/wake_button_sampler.h"
#include "config.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_wake_stub.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_IDF_TARGET_ESP32S3 && CONFIG_ULP_COPROC_TYPE_FSM
#include "esp32s3/rom/rtc.h"
#include "esp32s3/ulp.h"
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc.h"

#define SAMPLE_PERIOD_US 10000
#define SAMPLE_COUNT (POWER_ON_HOLD_MS / 10)
#define SAMPLE_RESULT_WORD 126
#define SAMPLE_COUNT_WORD 127
#define SAMPLER_MAGIC 0x57414b45U
#define SAMPLER_MEM ((volatile uint32_t *)RTC_SLOW_MEM)
_Static_assert(CONFIG_ULP_COPROC_RESERVE_MEM >= 512, "Wake sampler needs 512 RTC bytes");
_Static_assert(POWER_ON_HOLD_MS % 10 == 0, "Wake hold must use whole 10 ms samples");

static RTC_DATA_ATTR uint32_t sampler_prepared;
static RTC_DATA_ATTR uint32_t sampler_started;
static RTC_DATA_ATTR uint32_t sampler_pin_mask;

// Same register sequence as ESP-IDF's ulp_run(), restricted to ROM/RTC code.
// The ULP runs alongside the ROM bootloader and stops itself after its verdict.
void RTC_IRAM_ATTR esp_wake_deep_sleep(void) {
  esp_default_wake_deep_sleep();
  sampler_started = 0;
  if (sampler_prepared != SAMPLER_MAGIC || !(esp_wake_stub_get_wakeup_cause() & EXT_EVENT1_TRIG) ||
      !(REG_GET_FIELD(RTC_CNTL_EXT_WAKEUP1_STATUS_REG, RTC_CNTL_EXT_WAKEUP1_STATUS) & sampler_pin_mask))
    return;

  SET_PERI_REG_MASK(RTC_CNTL_ULP_CP_CTRL_REG, RTC_CNTL_ULP_CP_RESET);
  esp_rom_delay_us(20);
  CLEAR_PERI_REG_MASK(RTC_CNTL_ULP_CP_CTRL_REG, RTC_CNTL_ULP_CP_RESET);
  CLEAR_PERI_REG_MASK(RTC_CNTL_ULP_CP_TIMER_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN);
  esp_rom_delay_us(10);
  REG_SET_FIELD(RTC_CNTL_ULP_CP_TIMER_REG, RTC_CNTL_ULP_CP_PC_INIT, 0);
  SET_PERI_REG_MASK(RTC_CNTL_COCPU_CTRL_REG, RTC_CNTL_COCPU_SEL);
  SET_PERI_REG_MASK(RTC_CNTL_ULP_CP_CTRL_REG, RTC_CNTL_ULP_CP_CLK_FO);
  CLEAR_PERI_REG_MASK(RTC_CNTL_COCPU_CTRL_REG, RTC_CNTL_COCPU_DONE_FORCE);
  SET_PERI_REG_MASK(RTC_CNTL_COCPU_CTRL_REG, RTC_CNTL_COCPU_CLKGATE_EN);
  REG_WRITE(RTC_CNTL_INT_CLR_REG, RTC_CNTL_COCPU_INT_CLR | RTC_CNTL_COCPU_TRAP_INT_CLR | RTC_CNTL_ULP_CP_INT_CLR);
  CLEAR_PERI_REG_MASK(RTC_CNTL_ULP_CP_CTRL_REG, RTC_CNTL_ULP_CP_FORCE_START_TOP);
  sampler_started = SAMPLER_MAGIC;
  SET_PERI_REG_MASK(RTC_CNTL_ULP_CP_TIMER_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN);
}

esp_err_t wake_button_sampler_prepare(int gpio, int active_level) {
  sampler_prepared = sampler_started = 0;
  ulp_timer_stop();
  CLEAR_PERI_REG_MASK(RTC_CNTL_ULP_CP_CTRL_REG, RTC_CNTL_ULP_CP_CLK_FO);
  if (gpio < 0)
    return ESP_OK;
  if (gpio > 21 || (active_level != 0 && active_level != 1))
    return ESP_ERR_INVALID_ARG;

  // Each timer tick checks the pin, advances a retained count, then halts.
  // Either release or SAMPLE_COUNT held samples disables all future ticks.
  const ulp_insn_t program[] = {
      I_RD_REG(RTC_GPIO_IN_REG, gpio + RTC_GPIO_IN_NEXT_S, gpio + RTC_GPIO_IN_NEXT_S),
      I_SUBI(R0, R0, active_level),
      M_BL(1, 1),
      I_MOVI(R0, WAKE_SAMPLE_CANCELLED),
      M_BX(2),
      M_LABEL(1),
      I_MOVI(R3, SAMPLE_COUNT_WORD),
      I_LD(R0, R3, 0),
      I_ADDI(R0, R0, 1),
      I_ST(R0, R3, 0),
      M_BL(3, SAMPLE_COUNT),
      I_MOVI(R0, WAKE_SAMPLE_CONFIRMED),
      M_LABEL(2),
      I_MOVI(R3, SAMPLE_RESULT_WORD),
      I_ST(R0, R3, 0),
      I_END(),
      M_LABEL(3),
      I_HALT(),
  };
  size_t words = sizeof(program) / sizeof(program[0]);
  esp_err_t err = ulp_process_macros_and_load(0, program, &words);
  if (err != ESP_OK)
    return err;
  err = ulp_set_wakeup_period(0, SAMPLE_PERIOD_US);
  if (err != ESP_OK)
    return err;
  SAMPLER_MEM[SAMPLE_RESULT_WORD] = WAKE_SAMPLE_PENDING;
  SAMPLER_MEM[SAMPLE_COUNT_WORD] = 0;
  sampler_pin_mask = BIT(gpio);
  sampler_prepared = SAMPLER_MAGIC;
  return ESP_OK;
}

WakeButtonSample wake_button_sampler_result(void) {
  if (sampler_started != SAMPLER_MAGIC)
    return WAKE_SAMPLE_UNAVAILABLE;
  uint32_t result = SAMPLER_MEM[SAMPLE_RESULT_WORD] & UINT16_MAX;
  return result >= WAKE_SAMPLE_PENDING && result <= WAKE_SAMPLE_CANCELLED ? (WakeButtonSample)result
                                                                         : WAKE_SAMPLE_CANCELLED;
}

void wake_button_sampler_finish(void) {
  if (sampler_started != SAMPLER_MAGIC)
    return;
  int64_t deadline = esp_timer_get_time() + POWER_ON_HOLD_MS * 1000 + 100000;
  while (wake_button_sampler_result() == WAKE_SAMPLE_PENDING && esp_timer_get_time() < deadline) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  ulp_timer_stop();
  esp_rom_delay_us(100); // Let an in-flight instruction retire before stopping its clock.
  CLEAR_PERI_REG_MASK(RTC_CNTL_ULP_CP_CTRL_REG, RTC_CNTL_ULP_CP_CLK_FO);
  if (wake_button_sampler_result() == WAKE_SAMPLE_PENDING)
    SAMPLER_MEM[SAMPLE_RESULT_WORD] = WAKE_SAMPLE_CANCELLED;
  ESP_LOGI("PUBREMOTE-WAKE", "ULP hold result=%u samples=%u", (unsigned)wake_button_sampler_result(),
           (unsigned)(SAMPLER_MEM[SAMPLE_COUNT_WORD] & UINT16_MAX));
}
#else
#error "500 ms overlapping button wake requires ESP32-S3 with ULP FSM enabled"
#endif
