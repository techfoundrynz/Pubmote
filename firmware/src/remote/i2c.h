#ifndef __I2C_H
#define __I2C_H

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "stdio.h"

#ifdef __cplusplus
extern "C"
{
#endif

  i2c_master_bus_handle_t i2c_get_bus_handle();
  esp_err_t i2c_write(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t len, int timeout_ms);
  esp_err_t i2c_read(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t len, int timeout_ms);
  void init_i2c();

#ifdef __cplusplus
}
#endif

#endif