#include "i2c.h"
#include "config.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "PUBREMOTE-I2C";

#define I2C_MASTER_NUM I2C_NUM_0

// Global handles for new API
static i2c_master_bus_handle_t i2c_bus_handle = NULL;

// Device handle cache - we'll create handles as needed
typedef struct {
  uint8_t device_addr;
  i2c_master_dev_handle_t handle;
} device_handle_cache_t;

// Simple cache for device handles (max 8 devices)
static device_handle_cache_t device_cache[8];
static int device_cache_count = 0;

// Helper function to get or create device handle
static i2c_master_dev_handle_t get_device_handle(uint8_t device_addr) {
  // Check if we already have a handle for this device
  for (int i = 0; i < device_cache_count; i++) {
    if (device_cache[i].device_addr == device_addr) {
      return device_cache[i].handle;
    }
  }

  // Create new device handle if cache not full
  if (device_cache_count < 8) {
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_addr,
        .scl_speed_hz = I2C_SCL_FREQ_HZ,
    };

    i2c_master_dev_handle_t new_handle;
    if (i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &new_handle) == ESP_OK) {
      device_cache[device_cache_count].device_addr = device_addr;
      device_cache[device_cache_count].handle = new_handle;
      device_cache_count++;
      return new_handle;
    }
  }

  return NULL;
}

i2c_master_bus_handle_t i2c_get_bus_handle() {
  if (i2c_bus_handle == NULL) {
    ESP_LOGE(TAG, "I2C bus handle is not initialized");
    return NULL;
  }
  return i2c_bus_handle;
}

esp_err_t i2c_write(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t len, int timeout_ms) {
  if (i2c_bus_handle == NULL) {
    ESP_LOGE(TAG, "I2C bus not initialized");
    return ESP_FAIL;
  }

  esp_err_t ret = ESP_OK;
  i2c_master_dev_handle_t dev_handle = get_device_handle(device_addr);

  if (dev_handle == NULL) {
    ESP_LOGE(TAG, "Failed to get device handle for address 0x%02X", device_addr);
    return ESP_FAIL;
  }

  // Prepare write buffer: register address + data
  uint8_t *write_buf = malloc(1 + len);
  if (write_buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate write buffer");
    return ESP_ERR_NO_MEM;
  }

  write_buf[0] = reg_addr;
  if (len > 0 && data != NULL) {
    memcpy(&write_buf[1], data, len);
  }

  // Execute the write
  ret = i2c_master_transmit(dev_handle, write_buf, 1 + len, timeout_ms);

  // Clean up
  free(write_buf);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
  }

  return ret;
}

esp_err_t i2c_read(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, size_t len, int timeout_ms) {
  if (i2c_bus_handle == NULL) {
    ESP_LOGE(TAG, "I2C bus not initialized");
    return ESP_FAIL;
  }

  if (data == NULL || len == 0) {
    ESP_LOGE(TAG, "Invalid read parameters");
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = ESP_OK;
  i2c_master_dev_handle_t dev_handle = get_device_handle(device_addr);

  if (dev_handle == NULL) {
    ESP_LOGE(TAG, "Failed to get device handle for address 0x%02X", device_addr);
    return ESP_FAIL;
  }

  // Execute the read (write register address, then read data)
  ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, len, timeout_ms);

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
  }

  return ret;
}

void init_i2c() {
#if defined(I2C_SDA) && defined(I2C_SCL)
  // Initialize I2C master bus
  i2c_master_bus_config_t i2c_bus_config = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = I2C_MASTER_NUM,
      .scl_io_num = I2C_SCL,
      .sda_io_num = I2C_SDA,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = false,
  };

  ESP_LOGI(TAG, "Initializing I2C for display touch");
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle));

  // Initialize device cache
  device_cache_count = 0;
  memset(device_cache, 0, sizeof(device_cache));
#endif
}

// Optional: Cleanup function for proper resource management
void deinit_i2c() {
  // Remove all cached devices
  for (int i = 0; i < device_cache_count; i++) {
    if (device_cache[i].handle != NULL) {
      i2c_master_bus_rm_device(device_cache[i].handle);
    }
  }
  device_cache_count = 0;

  // Delete the bus
  if (i2c_bus_handle != NULL) {
    i2c_del_master_bus(i2c_bus_handle);
    i2c_bus_handle = NULL;
  }
}