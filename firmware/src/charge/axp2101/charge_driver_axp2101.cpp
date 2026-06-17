#include "charge_driver_axp2101.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include <cstring>
#include <stdio.h>
// Define the configuration for XPowersLib
#define CONFIG_XPOWERS_ESP_IDF_NEW_API
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include "config.h"
#include "remote/i2c.h"
#include <charge/charge_driver.h>
#include <driver/gpio.h>

static const char *TAG = "PUBREMOTE-CHARGE_DRIVER_AXP2101";

// AXP2101 I2C address (0x34 is the typical address)
#define AXP2101_ADDR 0x34
#define AXP2101_DEBUG 1

// AXP2101 instance
static XPowersPMU PMU;

static int axp2101_read_reg(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
  esp_err_t result = i2c_read(device_addr, reg_addr, data, len, 500);
  return (result == ESP_OK) ? 0 : -1; // XPowersLib expects 0=success, -1=failure
}

static int axp2101_write_reg(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
  esp_err_t result = i2c_write(device_addr, reg_addr, data, len, 500);
  return (result == ESP_OK) ? 0 : -1; // XPowersLib expects 0=success, -1=failure
}

/**
 * @brief Initialize the AXP2101 power management chip
 */
static esp_err_t axp2101_init() {
  // Initialize the AXP2101 with the XPowersLib
  bool result = PMU.begin(AXP2101_ADDR, axp2101_read_reg, axp2101_write_reg);
  if (!result) {
    ESP_LOGE(TAG, "Failed to initialize AXP2101");
    return ESP_FAIL;
  }

  PMU.init();
  PMU.setSysPowerDownVoltage(MIN_BATTERY_VOLTAGE); // Low voltage protection
  PMU.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
  PMU.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_1000MA);
  PMU.enableWatchdog();
  

  ESP_LOGI(TAG, "AXP2101 initialized successfully");
  return ESP_OK;
}

extern "C" esp_err_t axp2101_charge_driver_init() {
  ESP_LOGI(TAG, "Initializing AXP2101 charge driver");

  // Initialize AXP2101
  esp_err_t ret = axp2101_init();
  if (ret != ESP_OK) {
    return ret;
  }

  ESP_LOGI(TAG, "AXP2101 charge driver initialized successfully");
  return ESP_OK;
}

#if AXP2101_DEBUG
// Define debug logging functions here
#endif

extern "C" RemotePowerState axp2101_get_power_state() {
  PMU.clrWatchdog();
  RemotePowerState state = {.voltage = 0, .chargeState = CHARGE_STATE_UNKNOWN, .current = 0, .isPowered = false, .isFault = false};
  state.voltage = PMU.getBattVoltage();
  state.current = 0; // AXP2101 does not provide current reading directly

  xpowers_chg_status_t status = PMU.getChargerStatus();

  switch (status) {
  case XPOWERS_AXP2101_CHG_STOP_STATE: // No charge
    state.chargeState = CHARGE_STATE_NOT_CHARGING;
    break;
  case XPOWERS_AXP2101_CHG_PRE_STATE: // Pre-charge
    state.chargeState = CHARGE_STATE_CHARGING;
    break;
  case XPOWERS_AXP2101_CHG_CC_STATE: // Constant-current charge
    state.chargeState = CHARGE_STATE_CHARGING;
    break;
  case XPOWERS_AXP2101_CHG_CV_STATE: // Constant-voltage charge
    state.chargeState = CHARGE_STATE_CHARGING;
    break;
  case XPOWERS_AXP2101_CHG_DONE_STATE: // Charge done
    state.chargeState = CHARGE_STATE_DONE;
    break;
  default:
    state.chargeState = CHARGE_STATE_UNKNOWN;
    break;
  }

  state.isPowered = PMU.isVbusIn();
  state.isFault = false;


  ESP_LOGD(TAG, "\nVBUS: %s %04dmV\nVBAT: %04dmV\nVSYS: %04dmV",
           PMU.isVbusIn() ? "Connected" : "Disconnect", PMU.getVbusVoltage(), PMU.getBattVoltage(),
           PMU.getSystemVoltage());

  return state;
}

void axp2101_disable_watchdog() {
  PMU.disableWatchdog();
  ESP_LOGI(TAG, "AXP2101 watchdog disabled");
}

void axp2101_enable_watchdog() {
  PMU.enableWatchdog();
  ESP_LOGI(TAG, "AXP2101 watchdog enabled");
}