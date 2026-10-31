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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "remote/i2c.h"
#include <atomic>
#include <charge/charge_driver.h>
#include <driver/gpio.h>

static const char *TAG = "PUBREMOTE-CHARGE_DRIVER_SY6970";

// SY6970 I2C address (0x6A is the typical address)
#define SY6970_ADDR 0x6A
#define SY6970_DEBUG 0

extern "C" bool sy6970_probe_wake_power(bool *connected) {
  if (!connected)
    return false;
  uint8_t status, fault;
  // PG_STAT is valid with the ADC off. Two REG0C reads acknowledge the latched
  // and current IRQ without restarting DPDM, charging, ADC or watchdog timers.
  if (i2c_read(SY6970_ADDR, 0x0B, &status, 1, 20) != ESP_OK ||
      i2c_read(SY6970_ADDR, 0x0C, &fault, 1, 20) != ESP_OK ||
      i2c_read(SY6970_ADDR, 0x0C, &fault, 1, 20) != ESP_OK)
    return false;
  *connected = (status & 0x04) != 0;
  return true;
}

// SY6970 instance
static XPowersPPM PPM;
static portMUX_TYPE fault_lock = portMUX_INITIALIZER_UNLOCKED;
static int16_t sampled_fault_latched = -1;
static int16_t sampled_fault_current = -1;
static uint32_t fault_sample_ms = 0;
static int16_t last_fault = 0;
static uint32_t last_fault_ms = 0;
static ChargeDiagnostics cached_diagnostics = {};
static std::atomic<bool> diagnostics_requested(false);

static void refresh_diagnostics();

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

  // Read current register to preserve other settings
  int current_reg08 = PPM.readRegister(0x08);
  if (current_reg08 < 0)
    return ESP_FAIL;

  // Update only the IR compensation bits
  uint8_t new_reg08 = (current_reg08 & 0x03) | (bat_comp << 5) | (vclamp << 2);

  // Write back
  bool success = PPM.writeRegister(0x08, new_reg08) == 0;

  if (success) {
    ESP_LOGI(TAG, "IR Compensation set: %dmΩ, Clamp: %dmV\n", bat_comp * 20, vclamp * 32);
  }

  return success ? ESP_OK : ESP_FAIL;
}

esp_err_t sy6970_enter_protection_mode() {
  const int power = PPM.readRegister(0x0B);
  const int config = PPM.readRegister(0x09);
  if (power < 0 || config < 0) {
    return ESP_FAIL;
  }
  if (power & 0x04) {
    return ESP_ERR_INVALID_STATE;
  }
  ESP_LOGI(TAG, "Entering shipping mode (power down)");
  // Immediate BATFET cutoff, preserving unrelated timer/reset configuration.
  return PPM.writeRegister(0x09, (config & ~0x08) | 0x20) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t set_advanced_charging_config() {
  // Read and update REG05H: Bits 3:0 are Termination Current Limit. 0000 = 64mA.
  int reg05 = PPM.readRegister(0x05);
  if (reg05 != -1) {
    reg05 = (reg05 & 0xF0) | 0x00; // 64mA termination current (squeeze maximum charge)
    PPM.writeRegister(0x05, reg05);
    ESP_LOGI(TAG, "Termination current set to 64mA");
  }

  // Read and update REG06H: Bit 0 is Battery Recharge Threshold. 0 = 100mV.
  int reg06 = PPM.readRegister(0x06);
  if (reg06 != -1) {
    reg06 = reg06 & ~0x01; // Set VRECHG back to 100mV
    PPM.writeRegister(0x06, reg06);
    ESP_LOGI(TAG, "Recharge threshold set to VREG-100mV");
  }

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
  PPM.setChargeTargetVoltage(4224); // Increased from 4208 to push resting voltage closer to 4.20V
  PPM.setPrechargeCurr(128);
  PPM.setChargerConstantCurr(1024);
  PPM.enableAutoDetectionDPDM(); // Enable DPDM auto-detection
  PPM.disableHVDCP();
  PPM.enableMeasure(); // ADC must be enabled before reading voltages
  PPM.enableCharge();

  // Safe IR compensation to NOT trip hardware BATOVP (4.37V limit)
  set_ir_compensation(80, 128);
  set_advanced_charging_config();

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
static char *uint8_to_bits_static(uint8_t value) {
  static char buffer[9]; // 8 bits + null terminator
  for (int i = 7; i >= 0; i--) {
    buffer[7 - i] = ((value >> i) & 1) ? '1' : '0';
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
  RemotePowerState state = {
      .voltage = 0, .chargeState = CHARGE_STATE_UNKNOWN, .current = 0, .isPowered = false, .isFault = false};
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

  // PG_STAT is the live input-power status, independent of ADC conversions
  // and DPDM source classification. An OTG output is not a charger input.
  const int power_status = PPM.readRegister(0x0B);
  state.isPowered = power_status >= 0 && (power_status & 0x04) != 0;
  // Record entry into powered-but-not-charging without requiring a console
  // request or reboot. Do not restart charging or bypass a protection fault.
  static int previous_power_status = -1;
  if (power_status != previous_power_status && state.isPowered && state.chargeState != CHARGE_STATE_CHARGING &&
      state.chargeState != CHARGE_STATE_DONE) {
    uint8_t input = 0, config = 0, limits = 0;
    const int reg00 = i2c_read(SY6970_ADDR, 0x00, &input, 1, 100) == ESP_OK ? input : -1;
    const int reg03 = i2c_read(SY6970_ADDR, 0x03, &config, 1, 100) == ESP_OK ? config : -1;
    const int reg13 = i2c_read(SY6970_ADDR, 0x13, &limits, 1, 100) == ESP_OK ? limits : -1;
    ESP_LOGW(TAG, "USB powered but not charging: REG00=%d REG03=%d REG0B=%d REG13=%d (-1=read failed)", reg00, reg03,
             power_status, reg13);
  }
  previous_power_status = power_status;
  // The first read reports latched faults; the second reports current faults.
  // Preserve both: a transient fault may have cleared by the second read.
  const int fault_latched = PPM.readRegister(0x0C);
  const int fault_current = PPM.readRegister(0x0C);
  const uint32_t sampled_at = (uint32_t)(esp_timer_get_time() / 1000);
  portENTER_CRITICAL(&fault_lock);
  sampled_fault_latched = fault_latched;
  sampled_fault_current = fault_current;
  fault_sample_ms = sampled_at;
  if (fault_latched > 0 || fault_current > 0) {
    last_fault = fault_latched > 0 ? fault_latched : fault_current;
    last_fault_ms = sampled_at;
  }
  portEXIT_CRITICAL(&fault_lock);
  state.isFault = fault_latched > 0 || fault_current > 0;
  static int previous_latched = 0;
  static int previous_current = 0;
  if (fault_latched != previous_latched || fault_current != previous_current) {
    ESP_LOGW(TAG, "Charger fault registers: latched=%d current=%d (-1=read failed), charge=%d", fault_latched,
             fault_current, (int)state.chargeState);
    previous_latched = fault_latched;
    previous_current = fault_current;
  }

  ESP_LOGD(TAG,
           "\nVBUS: %s %04dmV\nVBAT: %04dmV\nVSYS: %04dmV\nBus state: %s\nCharge state: %s\nCharge Current: %04dmA",
           PPM.isVbusIn() ? "Connected" : "Disconnect", PPM.getVbusVoltage(), PPM.getBattVoltage(),
           PPM.getSystemVoltage(), PPM.getBusStatusString(), PPM.getChargeStatusString(), PPM.getChargeCurrent());
#if SY6970_DEBUG
  log_registers();
#endif

  if (diagnostics_requested.exchange(false))
    refresh_diagnostics();

  return state;
}

static void refresh_diagnostics() {
  ChargeDiagnostics sample = {};
  auto *diagnostics = &sample;
  diagnostics->chip = "SY6970";
  diagnostics->sample_ms = (uint32_t)(esp_timer_get_time() / 1000);
  portENTER_CRITICAL(&fault_lock);
  diagnostics->fault_latched = sampled_fault_latched;
  diagnostics->fault_current = sampled_fault_current;
  diagnostics->fault_sample_ms = fault_sample_ms;
  diagnostics->last_fault = last_fault;
  diagnostics->last_fault_ms = last_fault_ms;
  portEXIT_CRITICAL(&fault_lock);
  // Read configuration/status without writes or consuming fault latches.
  // This runs on the power task. Bound diagnostic timeouts so a missing
  // device cannot add seconds of delay to routine power management.
  for (uint8_t reg = 0; reg < 0x15; ++reg) {
    uint8_t value = 0;
    if (reg == 0x0C)
      diagnostics->registers[reg] = diagnostics->fault_current;
    else
      diagnostics->registers[reg] = i2c_read(SY6970_ADDR, reg, &value, 1, 20) == ESP_OK ? value : -1;
  }
  portENTER_CRITICAL(&fault_lock);
  cached_diagnostics = sample;
  portEXIT_CRITICAL(&fault_lock);
}

extern "C" bool sy6970_get_diagnostics(ChargeDiagnostics *diagnostics) {
  if (!diagnostics)
    return false;
  portENTER_CRITICAL(&fault_lock);
  *diagnostics = cached_diagnostics;
  portEXIT_CRITICAL(&fault_lock);
  if (diagnostics->sample_ms == 0) {
    diagnostics->chip = "SY6970";
    diagnostics->fault_latched = diagnostics->fault_current = -1;
    for (auto &reg : diagnostics->registers)
      reg = -1;
  }
  diagnostics_requested.store(true);
  return true;
}

void sy6970_disable_watchdog() {
  PPM.disableWatchdog();
  ESP_LOGI(TAG, "SY6970 watchdog disabled");
}

void sy6970_enable_watchdog() {
  PPM.enableWatchdog(PowersSY6970::TIMER_OUT_40SEC);
  ESP_LOGI(TAG, "SY6970 watchdog enabled");
}

void sy6970_charge_driver_deinit() {
  // Continuous ADC conversion (enabled in init) keeps converting off the
  // battery through deep sleep, costing tens to hundreds of uA. It gets
  // re-enabled by charge_driver_init on the next boot.
  PPM.disableADCMeasure();
  PPM.disableWatchdog();
  ESP_LOGI(TAG, "SY6970 deinitialized (ADC + watchdog disabled)");
}
