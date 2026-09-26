#include "game_store.h"
#include "cJSON.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ROOT "/littlefs/games"
static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex;
static bool mounted;
// Initialized before the console or UI can access the store.
static void lock_store(void) {
  if (!mutex) mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
  xSemaphoreTake(mutex, portMAX_DELAY);
}
static bool valid_id(const char *id) {
  size_t n = strlen(id);
  return n > 0 && n < sizeof(((game_info_t *)0)->id) &&
         strspn(id, "abcdefghijklmnopqrstuvwxyz0123456789_-") == n;
}
static bool mount_locked(void) {
  if (mounted) return true;
  esp_vfs_littlefs_conf_t conf = {.base_path = "/littlefs", .partition_label = "littlefs",
                                .format_if_mount_failed = false};
  if (esp_vfs_littlefs_register(&conf) != ESP_OK) return false;
  mounted = true;
  mkdir(ROOT, 0755);
  return true;
}
bool game_store_mount(void) {
  lock_store();
  bool ok = mount_locked();
  xSemaphoreGive(mutex);
  return ok;
}
bool game_store_metadata(const char *source, size_t length, game_info_t *info) {
  const char *prefix = "-- pubmote-game ";
  if (length < strlen(prefix) || memcmp(source, prefix, strlen(prefix))) return false;
  const char *end = memchr(source, '\n', length < 256 ? length : 256);
  if (!end) return false;
  char header[256];
  size_t n = end - source - strlen(prefix);
  memcpy(header, source + strlen(prefix), n);
  header[n] = 0;
  cJSON *json = cJSON_ParseWithOpts(header, NULL, true);
  const cJSON *api = cJSON_GetObjectItemCaseSensitive(json, "api");
  const cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "id");
  const cJSON *title = cJSON_GetObjectItemCaseSensitive(json, "title");
  const cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
  bool ok = cJSON_IsNumber(api) && api->valuedouble == 1 && cJSON_IsString(id) && valid_id(id->valuestring) &&
            cJSON_IsString(title) && strlen(title->valuestring) > 0 && strlen(title->valuestring) < sizeof(info->title) &&
            cJSON_IsString(version) && strlen(version->valuestring) > 0 && strlen(version->valuestring) < sizeof(info->version);
  if (ok) {
    strcpy(info->id, id->valuestring);
    strcpy(info->title, title->valuestring);
    strcpy(info->version, version->valuestring);
  }
  cJSON_Delete(json);
  return ok;
}
static char *read_locked(const char *id, size_t *length) {
  if (!valid_id(id) || !mount_locked()) return NULL;
  char path[96];
  snprintf(path, sizeof(path), ROOT "/%s.lua", id);
  struct stat st;
  if (stat(path, &st) || st.st_size <= 0 || st.st_size > GAME_MAX_BYTES) return NULL;
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  char *source = heap_caps_malloc(st.st_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!source) source = malloc(st.st_size + 1);
  bool ok = source && fread(source, 1, st.st_size, file) == (size_t)st.st_size;
  fclose(file);
  if (!ok) { free(source); return NULL; }
  source[st.st_size] = 0;
  *length = st.st_size;
  return source;
}
char *game_store_read(const char *id, size_t *length) {
  lock_store();
  char *source = read_locked(id, length);
  xSemaphoreGive(mutex);
  return source;
}
int game_store_list(game_info_t *games, int capacity) {
  lock_store();
  int count = 0;
  DIR *dir = mount_locked() ? opendir(ROOT) : NULL;
  struct dirent *entry;
  while (dir && count < capacity && (entry = readdir(dir))) {
    size_t n = strlen(entry->d_name);
    if (n < 5 || n >= 28 || strcmp(entry->d_name + n - 4, ".lua")) continue;
    char id[24];
    memcpy(id, entry->d_name, n - 4); id[n - 4] = 0;
    size_t length;
    char *source = read_locked(id, &length);
    if (source && game_store_metadata(source, length, &games[count]) && !strcmp(id, games[count].id)) count++;
    free(source);
  }
  if (dir) closedir(dir);
  xSemaphoreGive(mutex);
  return count;
}

// One bounded USB upload; staging never replaces the installed file before commit.
static FILE *upload;
static char upload_id[24], expected_hash[65];
static size_t expected_size, written;
static mbedtls_sha256_context hash;
static void abort_upload(void) {
  if (upload) fclose(upload);
  upload = NULL;
  unlink(ROOT "/.upload");
  mbedtls_sha256_free(&hash);
}
static int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
static int game_command(int argc, char **argv) {
  lock_store();
  bool ok = false;
  const char *message = "Invalid command or unformatted LittleFS";
  if (argc == 3 && !strcmp(argv[1], "format") && !strcmp(argv[2], "ERASE_LITTLEFS")) {
    abort_upload();
    ok = esp_littlefs_format("littlefs") == ESP_OK;
    if (ok) { ok = mount_locked(); mkdir(ROOT, 0755); }
  } else if (mount_locked()) {
    if (argc == 5 && !strcmp(argv[1], "begin") && valid_id(argv[2])) {
      abort_upload();
      char *end;
      unsigned long size = strtoul(argv[3], &end, 10);
      if (*argv[3] && !*end && size > 0 && size <= GAME_MAX_BYTES && strlen(argv[4]) == 64 &&
          strspn(argv[4], "0123456789abcdef") == 64) {
        size_t total, used;
        if (esp_littlefs_info("littlefs", &total, &used) == ESP_OK && total - used > size + 8192) {
          upload = fopen(ROOT "/.upload", "wb");
          if (upload) {
            strcpy(upload_id, argv[2]); strcpy(expected_hash, argv[4]); expected_size = size; written = 0;
            mbedtls_sha256_init(&hash); mbedtls_sha256_starts(&hash, 0); ok = true;
          }
        } else message = "Not enough filesystem space";
      }
    } else if (argc == 4 && !strcmp(argv[1], "chunk") && upload) {
      char *end;
      unsigned long offset = strtoul(argv[2], &end, 10);
      size_t chars = strlen(argv[3]);
      unsigned char bytes[256];
      ok = *argv[2] && !*end && offset == written && chars > 0 && chars % 2 == 0 && chars <= sizeof(bytes) * 2 &&
           written + chars / 2 <= expected_size;
      for (size_t i = 0; ok && i < chars / 2; i++) {
        int a = hex_digit(argv[3][i * 2]), b = hex_digit(argv[3][i * 2 + 1]);
        if (a < 0 || b < 0) ok = false;
        else bytes[i] = (a << 4) | b;
      }
      if (ok) {
        ok = fwrite(bytes, 1, chars / 2, upload) == chars / 2;
        if (ok) { mbedtls_sha256_update(&hash, bytes, chars / 2); written += chars / 2; }
      }
      if (!ok) abort_upload();
    } else if (argc == 2 && !strcmp(argv[1], "commit") && upload && written == expected_size) {
      unsigned char digest[32]; char hex[65];
      mbedtls_sha256_finish(&hash, digest);
      for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
      ok = !strcmp(hex, expected_hash) && fflush(upload) == 0;
      int close_result = fclose(upload); upload = NULL; ok = ok && close_result == 0;
      FILE *file = ok ? fopen(ROOT "/.upload", "rb") : NULL;
      char header[256] = {0}; game_info_t info;
      if (file) { size_t n = fread(header, 1, sizeof(header) - 1, file); fclose(file);
        ok = game_store_metadata(header, n, &info) && !strcmp(info.id, upload_id);
      } else ok = false;
      char path[96]; snprintf(path, sizeof(path), ROOT "/%s.lua", upload_id);
      if (ok) ok = rename(ROOT "/.upload", path) == 0;
      abort_upload();
    } else if (argc == 3 && !strcmp(argv[1], "remove") && valid_id(argv[2])) {
      char path[96]; snprintf(path, sizeof(path), ROOT "/%s.lua", argv[2]); ok = unlink(path) == 0;
    } else if (argc == 2 && !strcmp(argv[1], "abort")) { abort_upload(); ok = true; }
  }
  printf("GAME %s%s%s\n", ok ? "OK" : "ERROR", ok ? "" : " ", ok ? "" : message);
  xSemaphoreGive(mutex);
  return ok ? 0 : 1;
}
void game_store_register_console(void) {
  const esp_console_cmd_t cmd = {.command = "game", .help = "Install games using scripts/install_game.py", .func = game_command};
  esp_console_cmd_register(&cmd);
}
