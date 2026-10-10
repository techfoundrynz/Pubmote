#pragma once
#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct {
    char stable_url[512];     // Latest stable release asset URL
    char stable_tag[32];      // Stable release tag (e.g., "v1.0.0")
    char prerelease_url[512]; // Latest prerelease asset URL
    char prerelease_tag[32];  // Prerelease tag (e.g., "v1.1.0-beta")
    char nightly_url[512];    // Latest nightly build asset URL
    char nightly_tag[32];     // Nightly tag (e.g., "nightly")
    bool stable_found;        // True if stable asset was found
    bool prerelease_found;    // True if prerelease asset was found
    bool nightly_found;       // True if nightly asset was found
  } github_asset_urls_t;

  typedef struct {
    int major;
    int minor;
    int patch;
  } firmware_version_t;

  typedef void (*ota_progress_callback_t)(const char *status);
  typedef struct {
    bool (*cancelled)(void *context);
    // Atomically claim finalization against cancellation. Once accepted, the
    // caller must defer cancellation until finish returns.
    bool (*begin_commit)(void *context);
    void *context;
  } ota_control_t;

  esp_err_t fetch_all_asset_urls(const char *asset_name, github_asset_urls_t *result);
  firmware_version_t parse_version_string(const char *version_str);
  bool is_version_greater(const firmware_version_t *a, const firmware_version_t *b);
  // Cancellation aborts the image without changing the boot partition.
  esp_err_t apply_ota(const char *url, ota_progress_callback_t progress_callback, const ota_control_t *control);

#ifdef __cplusplus
}
#endif
