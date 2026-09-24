#pragma once

#include "esp_err.h"
#include "haptic/haptic_patterns.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief Initialize the DRV2605 haptic driver
   *
   * This function initializes the DRV2605 haptic feedback driver, configures
   * the I2C communication, sets up the LRA motor parameters, and performs
   * automatic calibration.
   *
   * @return ESP_OK on success, ESP_FAIL on error
   */
  esp_err_t drv2605_haptic_driver_init(void);

  /**
   * @brief Deinitialize the DRV2605 haptic driver
   *
   * This function stops any ongoing vibration, releases resources, and
   * optionally disables the haptic driver power pin.
   */
  void drv2605_haptic_driver_deinit(void);

  /**
   * @brief Play a specific vibration pattern
   *
   * This function configures and plays the specified haptic feedback pattern
   * using the DRV2605's built-in waveform library.
   *
   * @param pattern The haptic feedback pattern to play
   */
  void drv2605_haptic_play_vibration(HapticFeedbackPattern pattern);

  /**
   * @brief Stop any currently playing vibration
   *
   * This function immediately stops any ongoing haptic feedback.
   */
  void drv2605_haptic_stop(void);

  /**
   * @brief Check if vibration is currently active
   *
   * @return true if vibration is likely active, false otherwise
   */
  bool drv2605_is_active(void);

#ifdef __cplusplus
}
#endif