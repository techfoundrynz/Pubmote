#include "haptic_driver_drv2605.hpp"
#include "esp_err.h"
#include "esp_log.h"
#define CONFIG_SENSORLIB_ESP_IDF_NEW_API
#include "SensorDRV2605.hpp"
#include "remote/i2c.h"
#include <driver/gpio.h>
#include <memory>

static const char *TAG = "PUBREMOTE-HAPTIC_DRIVER_DRV2605";

// DRV2605 I2C address (0x5A is the typical address)
#define DRV2605_ADDR 0x5A

// LRA configuration parameters - STRONG BUT SAFE SETTINGS
#define LRA_RATED_VOLTAGE 1.2f     // 1.2V RMS
#define LRA_OVERDRIVE_VOLTAGE 2.4f // 2x rated voltage (safe maximum)
#define LRA_FREQUENCY 170.0f       // 170Hz ±5Hz
#define LRA_IMPEDANCE 9.0f         // 9Ω ±2Ω

// Convert voltage to register value (assuming 0-255 range for 0-5.6V)
#define VOLTAGE_TO_REG(voltage) (uint8_t)((voltage / 5.6f) * 255.0f)

static SensorDRV2605 drv;
static bool haptic_initialized = false;

static bool drv2605_write_reg(uint8_t reg_addr, const uint8_t *data, size_t len) {
  esp_err_t result = i2c_write(DRV2605_ADDR, reg_addr, (uint8_t *)data, len, 500);
  return (result == ESP_OK);
}

bool drv2605_read_reg(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t len) {
  esp_err_t result = i2c_read(device_addr, reg_addr, data, len, 500);
  return (result == ESP_OK);
}

static bool drv2605_reg_cb(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len, bool writeReg, bool isWrite) {
  if (isWrite) {
    return drv2605_write_reg(reg, buf, len);
  }
  else {
    return drv2605_read_reg(addr, reg, buf, len);
  }
}

void drv2605_haptic_play_vibration(HapticFeedbackPattern pattern) {
  if (!haptic_initialized) {
    ESP_LOGE(TAG, "DRV2605 not initialized");
    return;
  }

  ESP_LOGI(TAG, "Playing vibration pattern: %d", (int)pattern);

  // Clear any existing waveforms
  drv.stop();

  // Configure waveform based on pattern using available waveforms
  switch (pattern) {
  case HAPTIC_NONE:
    drv.stop(); // Stop any ongoing vibration
    return;     // Nothing to play
    break;
  case HAPTIC_SINGLE_CLICK:
    drv.setWaveform(0, 1);
    break;
  case HAPTIC_DOUBLE_CLICK:
    drv.setWaveform(0, 10);
    break;

  case HAPTIC_TRIPLE_CLICK:
    drv.setWaveform(0, 12);
    break;

  case HAPTIC_SOFT_BUMP:
    drv.setWaveform(0, 7);
    break;

  case HAPTIC_SOFT_BUZZ:
    drv.setWaveform(0, 13);
    break;

  case HAPTIC_STRONG_BUZZ:
    drv.setWaveform(0, 14);
    break;

  case HAPTIC_ALERT_750MS:
    drv.setWaveform(0, 15);
    break;

  case HAPTIC_ALERT_1000MS:
    drv.setWaveform(0, 16);
    break;

  default:
    ESP_LOGW(TAG, "Unknown vibration pattern: %d, using default", (int)pattern);
    break;
  }

  // Start the waveform sequence
  drv.run();

  ESP_LOGI(TAG, "Vibration pattern %d played successfully", (int)pattern);
}

// Define because of circular reference
static void schedule_calibration_cb();

static esp_timer_handle_t calibration_timer = NULL;

// We will use a state variable to track calibration progress
enum CalState {
  CAL_STATE_IDLE,
  CAL_STATE_START,
  CAL_STATE_WAITING
};
static CalState cal_state = CAL_STATE_IDLE;

static void calibration_callback(void *arg) {
  if (cal_state == CAL_STATE_START) {
    ESP_LOGI(TAG, "Starting deferred LRA auto-calibration...");

    // Temporarily disable haptic playback during calibration
    haptic_initialized = false;

    // Set mode to Auto Calibration
    drv.setMode(SensorDRV2605::MODE_AUTOCAL);

    // Start auto calibration by setting the GO bit (0x0C)
    uint8_t go = 1;
    drv2605_write_reg(0x0C, &go, 1);

    cal_state = CAL_STATE_WAITING;
    // Schedule next check in 50ms
    esp_timer_start_once(calibration_timer, 50 * 1000);
    return;
  }

  if (cal_state == CAL_STATE_WAITING) {
    // Read the GO register (0x0C) to check if calibration is complete
    uint8_t go = 0;
    drv2605_read_reg(DRV2605_ADDR, 0x0C, &go, 1);
    if (go & 0x01) {
      // Still running, reschedule the timer to check again in 20ms
      esp_timer_start_once(calibration_timer, 20 * 1000);
      return;
    }

    // Read bit 3 of register 0x00 to check if calibration failed
    uint8_t status = 0;
    drv2605_read_reg(DRV2605_ADDR, 0x00, &status, 1); // STATUS register
    if (status & 0x08) {
      ESP_LOGE(TAG, "LRA Auto-calibration failed (STATUS: 0x%02X)", status);
      uint8_t comp = 0, bemf = 0;
      drv2605_read_reg(DRV2605_ADDR, 0x18, &comp, 1);
      drv2605_read_reg(DRV2605_ADDR, 0x19, &bemf, 1);
      ESP_LOGE(TAG, "COMP: 0x%02X, BEMF: 0x%02X", comp, bemf);
    }
    else {
      uint8_t comp = 0, bemf = 0, bemf_gain = 0;
      drv2605_read_reg(DRV2605_ADDR, 0x18, &comp, 1);
      drv2605_read_reg(DRV2605_ADDR, 0x19, &bemf, 1);
      drv2605_read_reg(DRV2605_ADDR, 0x1F, &bemf_gain, 1);
      ESP_LOGI(TAG, "LRA Auto-calibration complete! COMP: 0x%02X, BEMF: 0x%02X, BEMF Gain: 0x%02X", comp, bemf,
               bemf_gain);
    }

    drv.setMode(SensorDRV2605::MODE_INTTRIG); // Switch back to internal trigger mode
    haptic_initialized = true;
    cal_state = CAL_STATE_IDLE;
    return;
  }
}

static void schedule_calibration_cb() {
  if (calibration_timer != NULL) {
    esp_timer_delete(calibration_timer);
    calibration_timer = NULL;
  }

  // Timer configuration
  esp_timer_create_args_t timer_args = {.callback = calibration_callback,
                                        .arg = NULL,
                                        .dispatch_method = ESP_TIMER_TASK,
                                        .name = "haptic_calibration",
                                        .skip_unhandled_events = false};

  // Create the timer
  esp_timer_create(&timer_args, &calibration_timer);

  // Set state to START and trigger after 3 seconds (3,000,000 microseconds)
  cal_state = CAL_STATE_START;
  esp_timer_start_once(calibration_timer, 3000 * 1000);
}

static esp_err_t drv2605_init() {
#ifdef HAPTIC_EN
  // Enable pin configuration
  gpio_reset_pin((gpio_num_t)HAPTIC_EN);
  gpio_set_direction((gpio_num_t)HAPTIC_EN, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)HAPTIC_EN, 1); // Enable haptic driver power
  ESP_LOGI(TAG, "Haptic enable pin configured and activated");

  // Small delay to allow power stabilization
  vTaskDelay(pdMS_TO_TICKS(50));
#endif

  drv.begin(drv2605_reg_cb);

  // Configure LRA auto calibration parameters
  uint8_t feedback = 0xBA; // LRA mode, FB_BRAKE_FACTOR=3, LOOP_GAIN=2, BEMF_GAIN=2
  uint8_t rated_voltage = VOLTAGE_TO_REG(LRA_RATED_VOLTAGE);
  uint8_t clamp_voltage = VOLTAGE_TO_REG(LRA_OVERDRIVE_VOLTAGE);
  uint8_t control1 = 0x98; // Drive time 2.9ms for 170Hz LRA
  uint8_t control2 = 0xF5; // Default values
  uint8_t control3 = 0x00; // Closed loop LRA

  drv2605_write_reg(0x1A, &feedback, 1);      // FEEDBACK
  drv2605_write_reg(0x16, &rated_voltage, 1); // RATEDV
  drv2605_write_reg(0x17, &clamp_voltage, 1); // CLAMPV
  drv2605_write_reg(0x1B, &control1, 1);      // CONTROL1
  drv2605_write_reg(0x1C, &control2, 1);      // CONTROL2
  drv2605_write_reg(0x1D, &control3, 1);      // CONTROL3

  drv.selectLibrary(6);                     // USE LRA library
  drv.setMode(SensorDRV2605::MODE_INTTRIG); // Switch back to internal trigger mode

  // Allow immediate haptic usage using initial settings
  haptic_initialized = true;

  // Schedule calibration to run 3 seconds after boot
  schedule_calibration_cb();

  return ESP_OK;
}

void drv2605_haptic_stop() {
  if (!haptic_initialized) {
    ESP_LOGW(TAG, "DRV2605 not initialized");
    return;
  }

  drv.stop();
}

bool drv2605_is_active() {
  // Simple check - return true if driver is initialized
  // Without access to hardware status, we can't determine actual playing state
  return haptic_initialized;
}

#define DRV2606_MAX_INIT_WAIT_MS 300
#define DRV2605_INIT_POLL_INTERVAL_MS 20
esp_err_t drv2605_haptic_driver_init() {
  ESP_LOGI(TAG, "Initializing DRV2605 haptic driver");

  esp_err_t ret = drv2605_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "DRV2605 initialization failed");
    haptic_initialized = false;
    return ret;
  }

  int waited_ms = 0;
  while (!haptic_initialized) {
    if (waited_ms >= DRV2606_MAX_INIT_WAIT_MS) {
      ESP_LOGE(TAG, "DRV2605 calibration timeout");
      break;
    }
    ESP_LOGI(TAG, "Waiting for DRV2605 calibration to complete...");
    waited_ms += DRV2605_INIT_POLL_INTERVAL_MS;
    vTaskDelay(pdMS_TO_TICKS(DRV2605_INIT_POLL_INTERVAL_MS));
  }

  ESP_LOGI(TAG, "DRV2605 haptic driver initialized successfully. Took %d ms", waited_ms);
  return ESP_OK;
}

void drv2605_haptic_driver_deinit() {
  ESP_LOGI(TAG, "Deinitializing DRV2605 haptic driver");

  drv.stop();
  vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to ensure stop command is processed
  haptic_initialized = false;

#ifdef HAPTIC_EN
  gpio_set_level((gpio_num_t)HAPTIC_EN, 0); // Disable haptic driver power
#endif

  ESP_LOGI(TAG, "DRV2605 haptic driver deinitialized");
}