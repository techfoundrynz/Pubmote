#include "update_client.h"
#include "cJSON.h"
#include "config.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include <esp_err.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "PUBMOTE-OTA";
#define MANIFEST_CAPACITY 2048

// Only accept release assets from our repository as the initial download URL.
// GitHub redirects are still followed by ESP-IDF with TLS verification enabled.
static bool valid_asset_url(const char *url) {
  const char *prefix = "https://github.com/techfoundrynz/";
  if (!url || strncmp(url, prefix, strlen(prefix)) != 0)
    return false;
  const char *repo = url + strlen(prefix);
  if (strncmp(repo, "Pubmote/releases/download/", sizeof("Pubmote/releases/download/") - 1) != 0 &&
      strncmp(repo, "pubmote/releases/download/", sizeof("pubmote/releases/download/") - 1) != 0)
    return false;
  size_t length = strlen(url);
  if (length >= 512 || length < 4 || strcmp(url + length - 4, ".bin") != 0)
    return false;
  for (const unsigned char *p = (const unsigned char *)url; *p; ++p) {
    if (*p <= 32 || *p >= 127 || *p == '?' || *p == '#' || *p == '\\')
      return false;
  }
  return true;
}

typedef struct {
  char *data;
  size_t length;
  bool overflow;
} manifest_response_t;

static esp_err_t manifest_event(esp_http_client_event_t *evt) {
  manifest_response_t *response = evt->user_data;
  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    if (evt->data_len < 0 || (size_t)evt->data_len >= MANIFEST_CAPACITY - response->length) {
      response->overflow = true;
      return ESP_FAIL;
    }
    memcpy(response->data + response->length, evt->data, evt->data_len);
    response->length += evt->data_len;
    response->data[response->length] = '\0';
  }
  return ESP_OK;
}

static bool read_channel(const cJSON *root, const char *name, char *url, size_t url_size, char *tag, size_t tag_size,
                         bool *found) {
  const cJSON *channel = cJSON_GetObjectItemCaseSensitive(root, name);
  if (cJSON_IsNull(channel))
    return true;
  if (!cJSON_IsObject(channel))
    return false;
  const cJSON *url_json = cJSON_GetObjectItemCaseSensitive(channel, "url");
  const cJSON *tag_json = cJSON_GetObjectItemCaseSensitive(channel, "tag");
  if (!cJSON_IsString(url_json) || !cJSON_IsString(tag_json) || !valid_asset_url(url_json->valuestring) ||
      strlen(url_json->valuestring) >= url_size || !tag_json->valuestring[0] ||
      strlen(tag_json->valuestring) >= tag_size)
    return false;
  strcpy(url, url_json->valuestring);
  strcpy(tag, tag_json->valuestring);
  *found = true;
  return true;
}

esp_err_t fetch_all_asset_urls(const char *asset_name, github_asset_urls_t *result) {
  if (!asset_name || !result)
    return ESP_ERR_INVALID_ARG;
  memset(result, 0, sizeof(*result));
  size_t board_len = strlen(asset_name);
  if (!board_len || board_len > 96 || strspn(asset_name, "abcdefghijklmnopqrstuvwxyz0123456789_") != board_len)
    return ESP_ERR_INVALID_ARG;
  char url[sizeof(API_BASE_URL) + 128];
  snprintf(url, sizeof(url), API_BASE_URL "/ota/v1/releases?board=%s", asset_name);
  manifest_response_t response = {0};
  response.data = heap_caps_calloc(1, MANIFEST_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!response.data)
    response.data = calloc(1, MANIFEST_CAPACITY);
  if (!response.data)
    return ESP_ERR_NO_MEM;
  esp_http_client_config_t config = {
      .url = url,
      .event_handler = manifest_event,
      .user_data = &response,
      .timeout_ms = 15000,
      .buffer_size = 1024,
      .buffer_size_tx = 512,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .disable_auto_redirect = true,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    free(response.data);
    return ESP_ERR_NO_MEM;
  }
  esp_http_client_set_header(client, "Accept", "application/json");
  esp_err_t err = esp_http_client_perform(client);
  int status = esp_http_client_get_status_code(client);
  // Release TLS/HTTP allocations before allocating the JSON tree.
  esp_http_client_cleanup(client);
  if (response.overflow || (err == ESP_OK && status != 200))
    err = ESP_ERR_INVALID_RESPONSE;
  if (err == ESP_OK) {
    cJSON *json = cJSON_ParseWithOpts(response.data, NULL, true);
    if (!cJSON_IsObject(json) ||
        !read_channel(json, "stable", result->stable_url, sizeof(result->stable_url), result->stable_tag,
                      sizeof(result->stable_tag), &result->stable_found) ||
        !read_channel(json, "prerelease", result->prerelease_url, sizeof(result->prerelease_url),
                      result->prerelease_tag, sizeof(result->prerelease_tag), &result->prerelease_found) ||
        !read_channel(json, "nightly", result->nightly_url, sizeof(result->nightly_url), result->nightly_tag,
                      sizeof(result->nightly_tag), &result->nightly_found)) {
      memset(result, 0, sizeof(*result));
      err = ESP_ERR_INVALID_RESPONSE;
    }
    else if (!result->stable_found && !result->prerelease_found && !result->nightly_found) {
      err = ESP_ERR_NOT_FOUND;
    }
    cJSON_Delete(json);
  }
  free(response.data);
  return err;
}

/**
 * Parse a version string of the form "vX.Y.Z" into its components
 *
 * @param version_str The version string to parse (e.g., "v1.2.3")
 * @param result Pointer to structure that will hold the parsed version
 */
firmware_version_t parse_version_string(const char *version_str) {
  firmware_version_t result = {0, 0, 0};

  if (version_str == NULL) {
    ESP_LOGE(TAG, "Invalid parameters to parse_version_string");
    return result;
  }
  if (version_str[0] == 'v' || version_str[0] == 'V') {
    version_str++; // Skip leading 'v'
  }

  sscanf(version_str, "%d.%d.%d", &result.major, &result.minor, &result.patch);
  ESP_LOGI(TAG, "Parsed version string '%s' into %d.%d.%d", version_str, result.major, result.minor, result.patch);
  return result;
}

/**
 * Compare two firmware versions
 * @param a Pointer to first version
 * @param b Pointer to second version
 * @return true if a > b, false otherwise
 */
bool is_version_greater(const firmware_version_t *a, const firmware_version_t *b) {
  if (a->major != b->major) {
    return a->major > b->major;
  }
  if (a->minor != b->minor) {
    return a->minor > b->minor;
  }
  return a->patch > b->patch;
}

typedef void (*ota_progress_callback_t)(const char *status);

esp_err_t apply_ota(const char *url, ota_progress_callback_t progress_callback) {
  if (!valid_asset_url(url))
    return ESP_ERR_INVALID_ARG;
  ESP_LOGI(TAG, "Starting advanced HTTPS OTA update");
  if (progress_callback) {
    progress_callback("Initializing OTA...");
  }

  esp_http_client_config_t config = {
      .url = url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 120000,
      .keep_alive_enable = true,
      .buffer_size = 4096,
      .buffer_size_tx = 1024,
  };

  esp_https_ota_config_t ota_config = {
      .http_config = &config,
  };

  esp_https_ota_handle_t https_ota_handle = NULL;
  esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed: %s", esp_err_to_name(err));
    return err;
  }

  if (progress_callback) {
    progress_callback("Connected to server...");
  }

  // Get and validate new firmware info
  esp_app_desc_t app_desc;
  err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_https_ota_read_img_desc failed: %s", esp_err_to_name(err));
    esp_https_ota_abort(https_ota_handle);
    return err;
  }

  ESP_LOGI(TAG, "New firmware info:");
  ESP_LOGI(TAG, "Project name: %s", app_desc.project_name);
  ESP_LOGI(TAG, "Version: %s", app_desc.version);
  ESP_LOGI(TAG, "Compiled: %s %s", app_desc.date, app_desc.time);
  ESP_LOGI(TAG, "ESP-IDF: %s", app_desc.idf_ver);

  // Download and flash firmware with progress
  int image_size = esp_https_ota_get_image_size(https_ota_handle);
  ESP_LOGI(TAG, "Image size: %d bytes", image_size);

  if (progress_callback) {
    progress_callback("Starting download...");
  }

  int last_reported_progress = -1; // Track to avoid spamming callback

  while (1) {
    err = esp_https_ota_perform(https_ota_handle);
    if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      break;
    }

    // Show download progress
    int data_read = esp_https_ota_get_image_len_read(https_ota_handle);
    int progress = 0;
    if (image_size > 0) {
      progress = (data_read * 100) / image_size;
      ESP_LOGI(TAG, "Downloading update... %d%% (%d/%d bytes)", progress, data_read, image_size);
    }
    else {
      ESP_LOGI(TAG, "Downloaded: %d bytes", data_read);
    }

    // Call progress callback (only if progress changed to avoid spam)
    if (progress_callback && progress != last_reported_progress) {
      char status_update[100];
      int data_read_kb = data_read / 1024;
      int image_size_kb = image_size / 1024;
      snprintf(status_update, sizeof(status_update), "Downloading...\n%d%%\n%d/%d KB", progress, data_read_kb,
               image_size_kb);
      progress_callback(status_update);
      last_reported_progress = progress;
    }

    // Small delay to avoid flooding logs
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (progress_callback) {
    progress_callback("Download complete\nValidating...");
  }

  if (err != ESP_OK || !esp_https_ota_is_complete_data_received(https_ota_handle)) {
    ESP_LOGE(TAG, "Complete data was not received.");
    if (err == ESP_OK)
      err = ESP_FAIL;
    esp_https_ota_abort(https_ota_handle);
  }
  else {
    err = esp_https_ota_finish(https_ota_handle);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "OTA upgrade successful");
      return ESP_OK;
    }
    else {
      if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
        ESP_LOGE(TAG, "Image validation failed - image is corrupted");
      }
      ESP_LOGE(TAG, "ESP HTTPS OTA upgrade failed: %s", esp_err_to_name(err));
    }
  }
  return err;
}
