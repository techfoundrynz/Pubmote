#include "remote/settings_store.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>
#define STORAGE_NAMESPACE "nvs"
static const char *TAG = "PUBREMOTE-SETTINGS-STORE";
static esp_err_t nvs_write(const char *key, void *value, nvs_type_t type, size_t length) {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    return err;
  }

  switch (type) {
  case NVS_TYPE_I8:
    err = nvs_set_i8(nvs_handle, key, *(int8_t *)value);
    break;
  case NVS_TYPE_U8:
    err = nvs_set_u8(nvs_handle, key, *(uint8_t *)value);
    break;
  case NVS_TYPE_I16:
    err = nvs_set_i16(nvs_handle, key, *(int16_t *)value);
    break;
  case NVS_TYPE_U16:
    err = nvs_set_u16(nvs_handle, key, *(uint16_t *)value);
    break;
  case NVS_TYPE_I32:
    err = nvs_set_i32(nvs_handle, key, *(int32_t *)value);
    break;
  case NVS_TYPE_U32:
    err = nvs_set_u32(nvs_handle, key, *(uint32_t *)value);
    break;
  case NVS_TYPE_I64:
    err = nvs_set_i64(nvs_handle, key, *(int64_t *)value);
    break;
  case NVS_TYPE_U64:
    err = nvs_set_u64(nvs_handle, key, *(uint64_t *)value);
    break;
  case NVS_TYPE_STR:
    err = nvs_set_str(nvs_handle, key, (char *)value);
    break;
  case NVS_TYPE_BLOB:
    err = nvs_set_blob(nvs_handle, key, value, length);
    break;
  default:
    ESP_LOGE(TAG, "Unsupported data type!");
    err = ESP_ERR_INVALID_ARG;
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write!");
    nvs_close(nvs_handle);
    return err;
  }
  else {
    ESP_LOGI(TAG, "Write done");
  }

  err = nvs_commit(nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit!");
    nvs_close(nvs_handle);
    return err;
  }
  else {
    ESP_LOGI(TAG, "Commit done");
  }

  nvs_close(nvs_handle);
  return err;
}

// Function to read from NVS
static esp_err_t nvs_read(const char *key, void *value, nvs_type_t type, size_t length) {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    return err;
  }

  switch (type) {
  case NVS_TYPE_I8:
    err = nvs_get_i8(nvs_handle, key, (int8_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %d", *(int8_t *)value);
    }
    break;
  case NVS_TYPE_U8:
    err = nvs_get_u8(nvs_handle, key, (uint8_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %u", *(uint8_t *)value);
    }
    break;
  case NVS_TYPE_I16:
    err = nvs_get_i16(nvs_handle, key, (int16_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %d", *(int16_t *)value);
    }
    break;
  case NVS_TYPE_U16:
    err = nvs_get_u16(nvs_handle, key, (uint16_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %u", *(uint16_t *)value);
    }
    break;
  case NVS_TYPE_I32:
    err = nvs_get_i32(nvs_handle, key, (int32_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %ld", *(int32_t *)value);
    }
    break;
  case NVS_TYPE_U32:
    err = nvs_get_u32(nvs_handle, key, (uint32_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %lu", *(uint32_t *)value);
    }
    break;
  case NVS_TYPE_I64:
    err = nvs_get_i64(nvs_handle, key, (int64_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %lld", *(int64_t *)value);
    }
    break;
  case NVS_TYPE_U64:
    err = nvs_get_u64(nvs_handle, key, (uint64_t *)value);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Read done, value = %llu", *(uint64_t *)value);
    }
    break;
  case NVS_TYPE_STR: {
    size_t required_size;
    // Get the size of the string first
    err = nvs_get_str(nvs_handle, key, NULL, &required_size);
    if (err == ESP_OK) {
      // Guard the caller's buffer: the stored string length is independent of
      // whatever the caller sized its buffer from (e.g. a separate length key),
      // so a mismatch/corruption must not overflow it. length==0 means unknown
      // (legacy callers) - fall back to trusting NVS.
      if (length != 0 && required_size > length) {
        ESP_LOGE(TAG, "NVS string '%s' (%u bytes) exceeds caller buffer (%u) - refusing", key, (unsigned)required_size,
                 (unsigned)length);
        err = ESP_ERR_INVALID_SIZE;
        break;
      }
      // Read directly into the caller buffer - no intermediate alloc/copy
      err = nvs_get_str(nvs_handle, key, (char *)value, &required_size);
      if (err == ESP_OK) {
        ESP_LOGI(TAG, "Read done, value = %s", (char *)value);
      }
    }
    break;
  }
  case NVS_TYPE_BLOB:
    err = nvs_get_blob(nvs_handle, key, value, &length);
    break;
  default:
    ESP_LOGE(TAG, "Unsupported data type!");
    err = ESP_ERR_INVALID_ARG;
  }

  switch (err) {
  case ESP_OK:
    ESP_LOGI(TAG, "Read done");
    break;
  case ESP_ERR_NVS_NOT_FOUND: // Never saved; callers fall back to defaults
    break;
  default:
    ESP_LOGE(TAG, "Error (%s) reading!", esp_err_to_name(err));
  }

  nvs_close(nvs_handle);
  return err;
}

// Function to write an integer to NVS
esp_err_t nvs_write_int(const char *key, uint32_t value) {
  return nvs_write(key, &value, NVS_TYPE_U32, 0);
}

esp_err_t nvs_write_str(const char *key, const char *value) {
  return nvs_write(key, (void *)value, NVS_TYPE_STR, strlen(value) + 1);
}

esp_err_t nvs_read_str(const char *key, char *out_value, size_t *length) {
  return nvs_read(key, out_value, NVS_TYPE_STR, *length);
}

// Function to write a blob to NVS
esp_err_t nvs_write_blob(const char *key, void *value, size_t length) {
  return nvs_write(key, value, NVS_TYPE_BLOB, length);
}

// Function to read an integer from NVS
esp_err_t nvs_read_int(const char *key, uint32_t *value) {
  return nvs_read(key, value, NVS_TYPE_U32, 0);
}

// Function to read a blob from NVS
esp_err_t nvs_read_blob(const char *key, void *value, size_t length) {
  return nvs_read(key, value, NVS_TYPE_BLOB, length);
}

esp_err_t reset_all_settings() {
  return nvs_flash_erase();
}
