#include "ota/update_client.h"
#include "cJSON.h"
#include "config.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_err.h>
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

static bool ota_cancelled(const ota_control_t *control) {
  return control && control->cancelled && control->cancelled(control->context);
}

esp_err_t apply_ota(const char *url, ota_progress_callback_t progress_callback, const ota_control_t *control) {
  if (!valid_asset_url(url))
    return ESP_ERR_INVALID_ARG;
  if (ota_cancelled(control))
    return ESP_ERR_INVALID_STATE;
  esp_http_client_config_t config = {
      .url = url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 5000,
      .buffer_size = 4096,
      .buffer_size_tx = 1024,
      .disable_auto_redirect = true,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client)
    return ESP_ERR_NO_MEM;
  esp_err_t err = ESP_FAIL;
  esp_ota_handle_t handle = 0;
  bool started = false;
  char *buffer = NULL;
  const esp_partition_t *partition = NULL;
  int64_t image_size = 0;
  size_t downloaded = 0;
  int last_reported_progress = -1;
  if (progress_callback)
    progress_callback("Connecting to server...");
  // Stream explicitly instead of using the HTTPS OTA header reader, which
  // retries socket timeouts internally without exposing a cancellation point.
  for (int redirects = 0; redirects <= 10; ++redirects) {
    if (ota_cancelled(control)) {
      err = ESP_ERR_INVALID_STATE;
      goto done;
    }
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
      goto done;
    if (ota_cancelled(control)) {
      err = ESP_ERR_INVALID_STATE;
      goto done;
    }
    image_size = esp_http_client_fetch_headers(client);
    if (ota_cancelled(control)) {
      err = ESP_ERR_INVALID_STATE;
      goto done;
    }
    if (image_size < 0) {
      err = ESP_FAIL;
      goto done;
    }
    int status = esp_http_client_get_status_code(client);
    if (status == 200)
      break;
    if (redirects == 10 || (status != 301 && status != 302 && status != 303 && status != 307 && status != 308)) {
      err = ESP_ERR_INVALID_RESPONSE;
      goto done;
    }
    err = esp_http_client_set_redirection(client);
    if (err != ESP_OK)
      goto done;
    // Keep certificate verification on every redirect and refuse HTTPS downgrade.
    if (esp_http_client_get_transport_type(client) != HTTP_TRANSPORT_OVER_SSL) {
      err = ESP_ERR_INVALID_RESPONSE;
      goto done;
    }
    esp_http_client_close(client);
  }
  partition = esp_ota_get_next_update_partition(NULL);
  if (!partition || image_size > partition->size) {
    err = ESP_ERR_INVALID_SIZE;
    goto done;
  }
  buffer = malloc(4096);
  if (!buffer) {
    err = ESP_ERR_NO_MEM;
    goto done;
  }
  if (ota_cancelled(control)) {
    err = ESP_ERR_INVALID_STATE;
    goto done;
  }
  err = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle);
  if (err != ESP_OK)
    goto done;
  started = true;
  while (true) {
    if (ota_cancelled(control)) {
      err = ESP_ERR_INVALID_STATE;
      goto done;
    }
    int count = esp_http_client_read(client, buffer, 4096);
    if (ota_cancelled(control)) {
      err = ESP_ERR_INVALID_STATE;
      goto done;
    }
    if (count <= 0) {
      if (count == 0 && esp_http_client_is_complete_data_received(client))
        break;
      // A stalled or truncated response fails instead of indefinitely retrying.
      err = count == -ESP_ERR_HTTP_EAGAIN ? ESP_ERR_TIMEOUT : ESP_FAIL;
      goto done;
    }
    if ((size_t)count > partition->size - downloaded) {
      err = ESP_ERR_INVALID_SIZE;
      goto done;
    }
    err = esp_ota_write(handle, buffer, count);
    if (err != ESP_OK)
      goto done;
    downloaded += count;
    int progress = image_size > 0 ? (int)(downloaded * 100 / image_size) : 0;
    if (progress_callback && progress != last_reported_progress) {
      char text[100];
      snprintf(text, sizeof(text), "Downloading...\n%d%%\n%u/%u KB", progress, (unsigned)(downloaded / 1024),
               (unsigned)(image_size / 1024));
      progress_callback(text);
      last_reported_progress = progress;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (!downloaded || (image_size > 0 && downloaded != (size_t)image_size)) {
    err = ESP_ERR_INVALID_RESPONSE;
    goto done;
  }
  // Free TLS before validating the image. The page atomically arbitrates Back
  // against this final commit, so cancellation cannot race boot selection.
  esp_http_client_cleanup(client);
  client = NULL;
  if (ota_cancelled(control) || (control && control->begin_commit && !control->begin_commit(control->context))) {
    err = ESP_ERR_INVALID_STATE;
    goto done;
  }
  if (progress_callback)
    progress_callback("Download complete\nValidating...");
  err = esp_ota_end(handle);
  started = false; // esp_ota_end releases the handle even on validation failure.
  if (err == ESP_OK)
    err = esp_ota_set_boot_partition(partition);
done:
  if (started)
    esp_ota_abort(handle);
  if (client)
    esp_http_client_cleanup(client);
  free(buffer);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "OTA stopped: %s", esp_err_to_name(err));
  return err;
}
