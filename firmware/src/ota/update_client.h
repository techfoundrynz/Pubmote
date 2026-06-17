#ifndef __RELEASE_CLIENT_H
#define __RELEASE_CLIENT_H
#include <esp_err.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
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

esp_err_t fetch_all_asset_urls(const char *asset_name, github_asset_urls_t *result);
firmware_version_t parse_version_string(const char *version_str);
bool is_version_greater(const firmware_version_t *a, const firmware_version_t *b);
esp_err_t apply_ota(const char *url, ota_progress_callback_t progress_callback);



#ifdef __cplusplus
}
#endif

#endif