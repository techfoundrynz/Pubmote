#include "update_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include <esp_err.h>
#include <esp_https_ota.h>
#include <esp_log.h>

static const char *TAG = "PUBMOTE-OTA";

#define GITHUB_API "https://api.github.com"
#define GITHUB_REPO "techfoundrynz/pubmote"
#define MAX_HTTP_OUTPUT_BUFFER 8192

// Escaped version of release_query.gql
static const char *GITHUB_ASSET_QUERY =
    "{"
    "\"query\": \"{"
    "repository(owner: \\\"techfoundrynz\\\", name: \\\"Pubmote\\\") {"
    "stable: latestRelease {"
    "name "
    "tagName "
    "assets: releaseAssets(first: 50) {"
    "nodes {"
    "name "
    "downloadUrl "
    "}"
    "}"
    "}"
    "prerelease: releases(first: 2, orderBy: {field: CREATED_AT, direction: DESC}) {"
    "nodes {"
    "name "
    "tagName "
    "isPrerelease "
    "assets: releaseAssets(first: 50) {"
    "nodes {"
    "name "
    "downloadUrl "
    "}"
    "}"
    "}"
    "}"
    "}"
    "}\""
    "}";

// Buffer to store HTTP response
static char *http_response_buffer = NULL;
static int http_response_len = 0;

// Helper function to check if asset is a .bin file
static bool is_bin_file(const char *filename) {
  if (filename == NULL) {
    return false;
  }

  int len = strlen(filename);
  if (len < 4) {
    return false;
  }

  // Check if filename ends with ".bin"
  return strcmp(filename + len - 4, ".bin") == 0;
}

// Helper function to match asset name and type
static bool matches_asset_criteria(const char *filename, const char *asset_name) {
  if (filename == NULL || asset_name == NULL) {
    return false;
  }

  // Must contain the asset name AND be a .bin file
  return (strstr(filename, asset_name) != NULL) && is_bin_file(filename);
}

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
  case HTTP_EVENT_ERROR:
    ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
    break;
  case HTTP_EVENT_HEADER_SENT:
    ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
    break;
  case HTTP_EVENT_ON_HEADER:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
    break;
  case HTTP_EVENT_ON_DATA:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

    // Allocate buffer for response if not already allocated (use PSRAM)
    if (http_response_buffer == NULL) {
      http_response_buffer = heap_caps_malloc(32768, MALLOC_CAP_SPIRAM);
      if (http_response_buffer == NULL) {
        // Fallback to internal memory if PSRAM fails
        http_response_buffer = malloc(32768);
      }
      http_response_len = 0;

      if (http_response_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate HTTP response buffer");
        return ESP_ERR_NO_MEM;
      }
    }

    // Copy data to buffer if there's space
    if (http_response_len + evt->data_len < 32768 - 1) {
      memcpy(http_response_buffer + http_response_len, evt->data, evt->data_len);
      http_response_len += evt->data_len;
      http_response_buffer[http_response_len] = '\0'; // Null terminate
    }
    else {
      ESP_LOGW(TAG, "HTTP response too large, truncating");
    }
    break;
  case HTTP_EVENT_ON_FINISH:
    ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
    break;
  case HTTP_EVENT_DISCONNECTED:
    ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
    break;
  case HTTP_EVENT_REDIRECT:
    ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
    break;
  }
  return ESP_OK;
}

/**
 * Fetch asset download URLs for stable, prerelease, and nightly builds
 *
 * @param asset_name The asset name to search for (substring matching)
 * @param result Pointer to structure that will hold all found URLs
 *
 * @return ESP_OK if at least one asset found, ESP_ERR_NOT_FOUND if none found, other error codes on failure
 */
esp_err_t fetch_all_asset_urls(const char *asset_name, github_asset_urls_t *result) {
  if (asset_name == NULL || result == NULL) {
    ESP_LOGE(TAG, "Invalid parameters");
    return ESP_ERR_INVALID_ARG;
  }

  // Initialize result structure
  memset(result, 0, sizeof(github_asset_urls_t));

  ESP_LOGI(TAG, "Searching for .bin asset: '%s' in all release types", asset_name);
  ESP_LOGI(TAG, "Note: .zip files will be ignored, only .bin files returned");

  const char *graphql_url = "https://api.github.com/graphql";

  // Reset response buffer
  if (http_response_buffer != NULL) {
    free(http_response_buffer);
    http_response_buffer = NULL;
    http_response_len = 0;
  }

  esp_http_client_config_t config = {
      .url = graphql_url,
      .event_handler = http_event_handler,
      .timeout_ms = 15000,
      .buffer_size = 4096,
      .buffer_size_tx = 4096,
      .method = HTTP_METHOD_POST,
      .crt_bundle_attach = esp_crt_bundle_attach, // This enables certificate verification
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);

  esp_http_client_set_header(client, "Accept", "application/json");
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "User-Agent", "ESP32-GitHub-Client");
#ifdef RELEASES_AUTH_TOKEN
  esp_http_client_set_header(client, "Authorization", "Bearer " RELEASES_AUTH_TOKEN);
#endif
  esp_http_client_set_post_field(client, GITHUB_ASSET_QUERY, strlen(GITHUB_ASSET_QUERY));

  esp_err_t err = esp_http_client_perform(client);
  esp_err_t function_result = ESP_ERR_NOT_FOUND; // Default to not found

  if (err == ESP_OK) {
    int status_code = esp_http_client_get_status_code(client);

    if (status_code == 200 && http_response_buffer != NULL) {
      ESP_LOGI(TAG, "GraphQL response: %d bytes", http_response_len);

      cJSON *json = cJSON_Parse(http_response_buffer);
      if (json != NULL) {
        cJSON *data = cJSON_GetObjectItem(json, "data");
        if (data && cJSON_GetObjectItem(data, "repository")) {
          cJSON *repository = cJSON_GetObjectItem(data, "repository");

          // Search in stable release
          cJSON *stable = cJSON_GetObjectItem(repository, "stable");
          if (stable && !cJSON_IsNull(stable)) {
            cJSON *tag_name = cJSON_GetObjectItem(stable, "tagName");
            cJSON *assets = cJSON_GetObjectItem(stable, "assets");

            ESP_LOGI(TAG, "Searching in stable release: %s",
                     cJSON_IsString(tag_name) ? tag_name->valuestring : "unknown");

            if (assets && cJSON_IsString(tag_name)) {
              cJSON *asset_nodes = cJSON_GetObjectItem(assets, "nodes");
              if (cJSON_IsArray(asset_nodes)) {
                int asset_count = cJSON_GetArraySize(asset_nodes);

                for (int i = 0; i < asset_count; i++) {
                  cJSON *asset = cJSON_GetArrayItem(asset_nodes, i);
                  cJSON *name = cJSON_GetObjectItem(asset, "name");
                  cJSON *download_url = cJSON_GetObjectItem(asset, "downloadUrl");

                  if (cJSON_IsString(name) && cJSON_IsString(download_url)) {
                    if (matches_asset_criteria(name->valuestring, asset_name)) {
                      ESP_LOGI(TAG, "Found asset in stable: %s", name->valuestring);

                      strncpy(result->stable_url, download_url->valuestring, sizeof(result->stable_url) - 1);
                      result->stable_url[sizeof(result->stable_url) - 1] = '\0';

                      strncpy(result->stable_tag, tag_name->valuestring, sizeof(result->stable_tag) - 1);
                      result->stable_tag[sizeof(result->stable_tag) - 1] = '\0';

                      result->stable_found = true;
                      function_result = ESP_OK;
                      break; // Take first match
                    }
                  }
                }
              }
            }
          }

          // Search in recent releases for prerelease and nightly
          cJSON *recent_releases = cJSON_GetObjectItem(repository, "prerelease");
          if (recent_releases) {
            cJSON *nodes = cJSON_GetObjectItem(recent_releases, "nodes");
            if (cJSON_IsArray(nodes)) {
              for (int i = 0; i < cJSON_GetArraySize(nodes); i++) {
                cJSON *release = cJSON_GetArrayItem(nodes, i);
                cJSON *tag_name = cJSON_GetObjectItem(release, "tagName");
                cJSON *assets = cJSON_GetObjectItem(release, "assets");

                if (!cJSON_IsString(tag_name))
                  continue;

                const char *tag_str = tag_name->valuestring;
                bool is_nightly = strcmp(tag_str, "nightly") == 0;

                if (assets) {
                  cJSON *asset_nodes = cJSON_GetObjectItem(assets, "nodes");
                  if (cJSON_IsArray(asset_nodes)) {
                    int asset_count = cJSON_GetArraySize(asset_nodes);

                    for (int j = 0; j < asset_count; j++) {
                      cJSON *asset = cJSON_GetArrayItem(asset_nodes, j);
                      cJSON *name = cJSON_GetObjectItem(asset, "name");
                      cJSON *download_url = cJSON_GetObjectItem(asset, "downloadUrl");

                      if (cJSON_IsString(name) && cJSON_IsString(download_url)) {
                        if (matches_asset_criteria(name->valuestring, asset_name)) {

                          // Categorize the release
                          if (is_nightly && !result->nightly_found) {
                            ESP_LOGI(TAG, "Found asset in nightly: %s (%s)", name->valuestring, tag_str);

                            strncpy(result->nightly_url, download_url->valuestring, sizeof(result->nightly_url) - 1);
                            result->nightly_url[sizeof(result->nightly_url) - 1] = '\0';

                            strncpy(result->nightly_tag, tag_str, sizeof(result->nightly_tag) - 1);
                            result->nightly_tag[sizeof(result->nightly_tag) - 1] = '\0';

                            result->nightly_found = true;
                            function_result = ESP_OK;
                          }
                          else if (!is_nightly && !result->prerelease_found) {
                            ESP_LOGI(TAG, "Found asset in prerelease: %s (%s)", name->valuestring, tag_str);

                            strncpy(result->prerelease_url, download_url->valuestring,
                                    sizeof(result->prerelease_url) - 1);
                            result->prerelease_url[sizeof(result->prerelease_url) - 1] = '\0';

                            strncpy(result->prerelease_tag, tag_str, sizeof(result->prerelease_tag) - 1);
                            result->prerelease_tag[sizeof(result->prerelease_tag) - 1] = '\0';

                            result->prerelease_found = true;
                            function_result = ESP_OK;
                          }

                          // Break inner loop if we found the asset in this release
                          break;
                        }
                      }
                    }
                  }
                }

                // Break if we found both prerelease and nightly
                if (result->prerelease_found && result->nightly_found) {
                  break;
                }
              }
            }
          }
        }

        cJSON_Delete(json);
      }
      else {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        function_result = ESP_ERR_INVALID_RESPONSE;
      }
    }
    else {
      ESP_LOGE(TAG, "HTTP request failed with status: %d", status_code);
      function_result = ESP_ERR_HTTP_CONNECT;
    }
  }
  else {
    ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    function_result = err;
  }

  // Cleanup
  esp_http_client_cleanup(client);
  if (http_response_buffer != NULL) {
    free(http_response_buffer);
    http_response_buffer = NULL;
  }

  // Log results
  ESP_LOGI(TAG, "=== SEARCH RESULTS FOR '%s' ===", asset_name);
  if (result->stable_found) {
    ESP_LOGI(TAG, "Stable: %s -> %s", result->stable_tag, result->stable_url);
  }
  else {
    ESP_LOGI(TAG, "Stable: Not found");
  }

  if (result->prerelease_found) {
    ESP_LOGI(TAG, "Prerelease: %s -> %s", result->prerelease_tag, result->prerelease_url);
  }
  else {
    ESP_LOGI(TAG, "Prerelease: Not found");
  }

  if (result->nightly_found) {
    ESP_LOGI(TAG, "Nightly: %s -> %s", result->nightly_tag, result->nightly_url);
  }
  else {
    ESP_LOGI(TAG, "Nightly: Not found");
  }

  if (function_result == ESP_ERR_NOT_FOUND) {
    ESP_LOGW(TAG, "Asset '%s' not found in any release type", asset_name);
  }

  return function_result;
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
  ESP_LOGI(TAG, "Starting advanced HTTPS OTA update");
  if (progress_callback) {
    progress_callback("Initializing OTA...");
  }

  esp_http_client_config_t config = {
      .url = url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 120000,
      .keep_alive_enable = true,
      .buffer_size = 8192,
      .buffer_size_tx = 4096,
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

  if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
    ESP_LOGE(TAG, "Complete data was not received.");
    err = ESP_FAIL;
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