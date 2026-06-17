#include "charge_driver.h"
#include "config.h"
#include <esp_log.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static const char *TAG = "PUBREMOTE-CHARGE-DRIVER";

#if PMU_SY6970
  #include "sy6970/charge_driver_sy6970.hpp"
#elif PMU_AXP2101
  #include "axp2101/charge_driver_axp2101.hpp"
#else
  #include "adc/charge_driver_adc.h"
#endif

esp_err_t charge_driver_init() {
  ESP_LOGI(TAG, "Init charge driver");
#if PMU_SY6970
  return sy6970_charge_driver_init();
#elif PMU_AXP2101
  return axp2101_charge_driver_init();
#else
  return adc_charge_driver_init();
#endif
}

RemotePowerState get_power_state() {
  ESP_LOGD(TAG, "Getting battery voltage");
#if PMU_SY6970
  return sy6970_get_power_state();
#elif PMU_AXP2101
  return axp2101_get_power_state();
#else
  return adc_get_power_state();
#endif
}

char *charge_state_to_string(RemoteChargeState state) {
  switch (state) {
  case CHARGE_STATE_NOT_CHARGING:
    return "Discharging";
  case CHARGE_STATE_CHARGING:
    return "Charging";
  case CHARGE_STATE_DONE:
    return "Complete";
  case CHARGE_STATE_UNKNOWN:
    return "Unknown";
  default:
    return "Invalid";
  }
}

static const VoltagePoint dischargeCurve[] = {
    {4150, 100}, // Fully charged (accounting for load drop)
    {4100, 90},  // Very high
    {4000, 78},  // High
    {3900, 66},  // Good
    {3800, 55},  // Above nominal
    {3750, 48},  // Near nominal
    {3700, 42},  // Nominal voltage point
    {3650, 36},  // Below nominal
    {3600, 30},  // Getting low
    {3500, 24},  // Low - still significant capacity remaining
    {3400, 15},  // Very low
    {3300, 8},   // Critical
    {3200, 3},   // Almost empty
    {3000, 0}    // Empty/cutoff (3.0V)
};

#define CURVE_SIZE (sizeof(dischargeCurve) / sizeof(VoltagePoint))

uint8_t battery_mv_to_percent(uint16_t voltage_mv) {
  // Handle edge cases
  if (voltage_mv >= dischargeCurve[0].voltage_mv) {
    return dischargeCurve[0].percentage;
  }
  if (voltage_mv <= dischargeCurve[CURVE_SIZE - 1].voltage_mv) {
    return dischargeCurve[CURVE_SIZE - 1].percentage;
  }

  // Find the two points to interpolate between
  for (int i = 0; i < (int)CURVE_SIZE - 1; i++) {
    if (voltage_mv <= dischargeCurve[i].voltage_mv && voltage_mv >= dischargeCurve[i + 1].voltage_mv) {
      // Linear interpolation between two points
      uint16_t v1 = dischargeCurve[i].voltage_mv;
      uint16_t v2 = dischargeCurve[i + 1].voltage_mv;
      uint8_t p1 = dischargeCurve[i].percentage;
      uint8_t p2 = dischargeCurve[i + 1].percentage;

      float percentage = p1 + ((float)(voltage_mv - v1) / (float)(v2 - v1)) * (p2 - p1);
      return (uint8_t)percentage; // Return as uint8_t to match percentage type
    }
  }

  return 0; // Fallback - should not reach here
}

void disable_watchdog() {
#if PMU_SY6970
  sy6970_disable_watchdog();
#elif PMU_AXP2101
  axp2101_disable_watchdog();
#else
  ESP_LOGW(TAG, "Watchdog not supported by ADC driver");
#endif
}

void enable_watchdog() {
#if PMU_SY6970
  sy6970_enable_watchdog();
#elif PMU_AXP2101
  axp2101_enable_watchdog();
#else
  ESP_LOGW(TAG, "Watchdog not supported by ADC driver");
#endif
}

void enter_protection_mode() {
#if PMU_SY6970
  sy6970_enter_protection_mode();
#endif
}