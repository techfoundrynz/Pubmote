#include "charge_driver_sy6970.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include <cstring>
#include <stdio.h>
// Define the configuration for XPowersLib
#define CONFIG_XPOWERS_ESP_IDF_NEW_API
#define XPOWERS_CHIP_SY6970
#include "XPowersLib.h"
#include "config.h"
#include "remote/i2c.h"
#include <charge/charge_driver.h>
#include <driver/gpio.h>

static const char *TAG = "PUBREMOTE-CHARGE_DRIVER_SY6970";

// SY6970 I2C address (0x6A is the typical address)
#define SY6970_ADDR 0x6A
#define SY6970_DEBUG 0

// SY6970 instance
static XPowersPPM PPM;

static int sy6970_read_reg(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
  esp_err_t result = i2c_read(device_addr, reg_addr, data, len, 500);
  return (result == ESP_OK) ? 0 : -1; // XPowersLib expects 0=success, -1=failure
}

static int sy6970_write_reg(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
  esp_err_t result = i2c_write(device_addr, reg_addr, data, len, 500);
  return (result == ESP_OK) ? 0 : -1; // XPowersLib expects 0=success, -1=failure
}

static esp_err_t set_ir_compensation(uint8_t resistance_mohm, uint8_t clamp_mv) {
    // Validate inputs
    if (resistance_mohm > 140) {
        ESP_LOGE(TAG, "Error: Resistance too high, max 140mΩ");
        return ESP_FAIL;
    }
    if (clamp_mv > 224) {
        ESP_LOGE(TAG, "Error: Clamp too high, max 224mV"); 
        return ESP_FAIL;
    }
    
    // Calculate register values
    uint8_t bat_comp = resistance_mohm / 20;
    uint8_t vclamp = clamp_mv / 32;
    uint8_t treg = 3; // Keep 120°C default
    
    // Read current register to preserve other settings
    uint8_t current_reg08 = PPM.readRegister(0x08);
    
    // Update only the IR compensation bits
    uint8_t new_reg08 = (current_reg08 & 0x03) | (bat_comp << 5) | (vclamp << 2);
    
    // Write back
    bool success = PPM.writeRegister(0x08, new_reg08);
    
    if (success) {
        ESP_LOGI(TAG, "IR Compensation set: %dmΩ, Clamp: %dmV\n", 
                     bat_comp * 20, vclamp * 32);
    }
    
    return success ? ESP_OK : ESP_FAIL;
}

esp_err_t sy6970_enter_protection_mode() {
    ESP_LOGI(TAG, "Entering shipping mode (power down)");
    PPM.shutdown();
    return ESP_OK;
}

/**
 * @brief Initialize the SY6970 power management chip
 */
static esp_err_t sy6970_init() {
  // Initialize the SY6970 with the XPowersLib
  bool result = PPM.begin(SY6970_ADDR, sy6970_read_reg, sy6970_write_reg);
  if (!result) {
    ESP_LOGE(TAG, "Failed to initialize SY6970");
    return ESP_FAIL;
  }

  PPM.init();
  PPM.enableWatchdog(PowersSY6970::TIMER_OUT_40SEC);
  PPM.setSysPowerDownVoltage(3500); // Default
  PPM.setInputCurrentLimit(1500);
  PPM.setChargeTargetVoltage(4208);
  PPM.setPrechargeCurr(128);
  PPM.setChargerConstantCurr(1024);
  PPM.enableAutoDetectionDPDM(); // Enable DPDM auto-detection
  PPM.enableHVDCP(); // Enable HVDCP detection
  PPM.setHighVoltageRequestedRange(PowersSY6970::REQUEST_9V); // Set high voltage request to 9V
  PPM.enableMeasure(); // ADC must be enabled before reading voltages
  PPM.enableCharge();
  set_ir_compensation(60, 96); // Set IR compensation to 60mOhm and 96mV clamp

  ESP_LOGI(TAG, "SY6970 initialized successfully");
  return ESP_OK;
}

extern "C" esp_err_t sy6970_charge_driver_init() {
  ESP_LOGI(TAG, "Initializing SY6970 charge driver");

  // Initialize SY6970
  esp_err_t ret = sy6970_init();
  if (ret != ESP_OK) {
    return ret;
  }

  ESP_LOGI(TAG, "SY6970 charge driver initialized successfully");
  return ESP_OK;
}

#if SY6970_DEBUG
static char* uint8_to_bits_static(uint8_t value) {
    static char buffer[9];  // 8 bits + null terminator
    for (int i = 7; i >= 0; i--) {
        buffer[7-i] = ((value >> i) & 1) ? '1' : '0';
    }
    buffer[8] = '\0';
    return buffer;
}


static void log_registers() {
  uint8_t reg1 = PPM.readRegister(POWERS_PPM_REG_01H);
  ESP_LOGI(TAG, "REG01H: %s", uint8_to_bits_static(reg1));
  uint8_t reg2 = PPM.readRegister(POWERS_PPM_REG_02H);
  ESP_LOGI(TAG, "REG02H: %s", uint8_to_bits_static(reg2));
  uint8_t reg3 = PPM.readRegister(POWERS_PPM_REG_03H);
  ESP_LOGI(TAG, "REG03H: %s", uint8_to_bits_static(reg3));
  uint8_t reg4 = PPM.readRegister(POWERS_PPM_REG_04H);
  ESP_LOGI(TAG, "REG04H: %s", uint8_to_bits_static(reg4));
  uint8_t reg5 = PPM.readRegister(POWERS_PPM_REG_05H);
  ESP_LOGI(TAG, "REG05H: %s", uint8_to_bits_static(reg5));
  uint8_t reg6 = PPM.readRegister(POWERS_PPM_REG_06H);
  ESP_LOGI(TAG, "REG06H: %s", uint8_to_bits_static(reg6));
  uint8_t reg7 = PPM.readRegister(POWERS_PPM_REG_07H);
  ESP_LOGI(TAG, "REG07H: %s", uint8_to_bits_static(reg7));
  uint8_t reg8 = PPM.readRegister(POWERS_PPM_REG_08H);
  ESP_LOGI(TAG, "REG08H: %s", uint8_to_bits_static(reg8));
  uint8_t reg9 = PPM.readRegister(POWERS_PPM_REG_09H);
  ESP_LOGI(TAG, "REG09H: %s", uint8_to_bits_static(reg9));
  uint8_t reg10 = PPM.readRegister(POWERS_PPM_REG_0AH);
  ESP_LOGI(TAG, "REG0AH: %s", uint8_to_bits_static(reg10));
  uint8_t reg11 = PPM.readRegister(POWERS_PPM_REG_0BH);
  ESP_LOGI(TAG, "REG0BH: %s", uint8_to_bits_static(reg11));
  uint8_t reg12 = PPM.readRegister(POWERS_PPM_REG_0CH);
  ESP_LOGI(TAG, "REG0CH: %s", uint8_to_bits_static(reg12));
  uint8_t reg13 = PPM.readRegister(POWERS_PPM_REG_0DH);
  ESP_LOGI(TAG, "REG0DH: %s", uint8_to_bits_static(reg13));
  uint8_t reg14 = PPM.readRegister(POWERS_PPM_REG_0EH);
  ESP_LOGI(TAG, "REG0EH: %s", uint8_to_bits_static(reg14));
  uint8_t reg15 = PPM.readRegister(POWERS_PPM_REG_0FH);
  ESP_LOGI(TAG, "REG0FH: %s", uint8_to_bits_static(reg15));
  uint8_t reg16 = PPM.readRegister(POWERS_PPM_REG_10H);
  ESP_LOGI(TAG, "REG10H: %s", uint8_to_bits_static(reg16));
  uint8_t reg17 = PPM.readRegister(POWERS_PPM_REG_11H);
  ESP_LOGI(TAG, "REG11H: %s", uint8_to_bits_static(reg17));
  uint8_t reg18 = PPM.readRegister(POWERS_PPM_REG_12H);
  ESP_LOGI(TAG, "REG12H: %s", uint8_to_bits_static(reg18));
  uint8_t reg19 = PPM.readRegister(POWERS_PPM_REG_13H);
  ESP_LOGI(TAG, "REG13H: %s", uint8_to_bits_static(reg19));
  uint8_t reg20 = PPM.readRegister(POWERS_PPM_REG_14H);
  ESP_LOGI(TAG, "REG14H: %s", uint8_to_bits_static(reg20));
}
#endif

extern "C" RemotePowerState sy6970_get_power_state() {
  PPM.feedWatchdog();
  RemotePowerState state = {.voltage = 0, .chargeState = CHARGE_STATE_UNKNOWN, .current = 0, .isPowered = false, .isFault = false};
  state.voltage = PPM.getBattVoltage();
  state.current = PPM.getChargeCurrent();

  PowersSY6970::ChargeStatus status = PPM.chargeStatus();

  switch (status) {
  case PowersSY6970::CHARGE_STATE_NO_CHARGE: // No charge
    state.chargeState = CHARGE_STATE_NOT_CHARGING;
    break;
  case PowersSY6970::CHARGE_STATE_PRE_CHARGE: // Pre-charge
    state.chargeState = CHARGE_STATE_CHARGING;
    break;
  case PowersSY6970::CHARGE_STATE_FAST_CHARGE: // Pre-charge
    state.chargeState = CHARGE_STATE_CHARGING;
    break;
  case PowersSY6970::CHARGE_STATE_DONE: // Charge done
    state.chargeState = CHARGE_STATE_DONE;
    break;
  default:
    state.chargeState = CHARGE_STATE_UNKNOWN;
    break;
  }

  state.isPowered = PPM.isVbusIn();
  state.isFault = PPM.getFaultStatus() != 0;

  if (state.isFault) {
    static uint8_t last_fault_status = 0;
    uint8_t current_fault_status = PPM.getFaultStatus();
    if (current_fault_status != last_fault_status) {
      ESP_LOGW(TAG, "SY6970 fault detected! Fault status: 0x%02X. Charge status: %s", current_fault_status, PPM.getChargeStatusString());
      last_fault_status = current_fault_status;
    }
  }


  ESP_LOGD(TAG, "\nVBUS: %s %04dmV\nVBAT: %04dmV\nVSYS: %04dmV\nBus state: %s\nCharge state: %s\nCharge Current: %04dmA",
           PPM.isVbusIn() ? "Connected" : "Disconnect", PPM.getVbusVoltage(), PPM.getBattVoltage(),
           PPM.getSystemVoltage(), PPM.getBusStatusString(), PPM.getChargeStatusString(), PPM.getChargeCurrent());
  #if SY6970_DEBUG
    log_registers();
  #endif

  return state;
}

void sy6970_disable_watchdog() {
  PPM.disableWatchdog();
  ESP_LOGI(TAG, "SY6970 watchdog disabled");
}

void sy6970_enable_watchdog() {
  PPM.enableWatchdog(PowersSY6970::TIMER_OUT_40SEC);
  ESP_LOGI(TAG, "SY6970 watchdog enabled");
}