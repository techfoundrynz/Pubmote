#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
  // Function to write an integer to NVS
  esp_err_t nvs_write_int(const char *key, uint32_t value);

  // Function to read an integer from NVS
  esp_err_t nvs_read_int(const char *key, uint32_t *value);

  // Function to write a string to NVS
  esp_err_t nvs_write_str(const char *key, const char *value);

  // Function to read a string from NVS
  esp_err_t nvs_read_str(const char *key, char *out_value, size_t *length);

  // Function to write a byte array to NVS
  esp_err_t nvs_write_blob(const char *key, void *value, size_t length);

  // Function to read a byte array from NVS
  esp_err_t nvs_read_blob(const char *key, void *value, size_t length);

  esp_err_t reset_all_settings();
#ifdef __cplusplus
}
#endif
