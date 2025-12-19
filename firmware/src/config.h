#ifndef __CONFIG_H
#define __CONFIG_H

// Set in env
// Powershell: $env:PLATFORMIO_BUILD_FLAGS='-D RELEASE_VARIANT=\"release\"'
#ifndef RELEASE_VARIANT
  #define RELEASE_VARIANT "dev"
#endif

#define MIN_BATTERY_VOLTAGE 3000
#define MAX_BATTERY_VOLTAGE 4200

#if defined(ACC1_POWER) && !defined(ACC1_POWER_ON_LEVEL)
  #define ACC1_POWER_ON_LEVEL 1
#endif

#if defined(ACC2_POWER) && !defined(ACC2_POWER_ON_LEVEL)
  #define ACC2_POWER_ON_LEVEL 1
#endif

#if defined(ACC2_POWER) && !defined(ACC2_POWER_DEFAULT_LEVEL)
  #define ACC2_POWER_DEFAULT 30 // Default to x% brightness
#endif

// i2c configuration
#if (!defined(I2C_SDA) || !defined(I2C_SCL))
  #error "I2C_SDA and I2C_SCL must be defined for I2C communication. Please define them in your build flags."
#endif

#define I2C_SCL_FREQ_HZ 100000

// Joystick configuration
#ifdef PRIMARY_BUTTON
  #define JOYSTICK_BUTTON_ENABLED 1
#else
  #define JOYSTICK_BUTTON_ENABLED 0
#endif

#if defined(JOYSTICK_X_ADC) && defined(JOYSTICK_X_ADC_UNIT)
  #define JOYSTICK_X_ENABLED 1
#else
  #define JOYSTICK_X_ENABLED 0
#endif

#if defined(JOYSTICK_Y_ADC) && defined(JOYSTICK_Y_ADC_UNIT)
  #define JOYSTICK_Y_ENABLED 1
#else
  #define JOYSTICK_Y_ENABLED 0
#endif

#define JOYSTICK_ENABLED (JOYSTICK_X_ENABLED || JOYSTICK_Y_ENABLED)

// Display configuration
#if defined(TP_CST816S) || defined(TP_FT3168) || defined(TP_CST9217)
  #define TOUCH_ENABLED 1
#else
  #define TOUCH_ENABLED 0
#endif

#ifndef TP_RST
  #define TP_RST -1
#endif

// Led configuration
#if defined(LED_DATA)
  #define LED_ENABLED 1
#else
  #define LED_ENABLED 0
#endif

#if LED_ENABLED && !defined(LED_COUNT)
  #define LED_COUNT 1 // Default to 1 LED if not defined
#endif

// Buzzer configuration
#if defined(BUZZER_PWM)
  #define BUZZER_ENABLED 1
#else
  #define BUZZER_ENABLED 0
#endif

#ifndef BUZZER_LEVEL
  #define BUZZER_LEVEL 1
#endif

// Haptic configuration
#if defined(HAPTIC_DRV2605) || defined(HAPTIC_PWM)
  #define HAPTIC_ENABLED 1
#else
  #define HAPTIC_ENABLED 0
#endif

#endif