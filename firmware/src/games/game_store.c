#include "game_store.h"
#include "cJSON.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "psa/crypto.h"
#include <dirent.h>
#include <fcntl.h>
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
  if (!mutex)
    mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
  xSemaphoreTake(mutex, portMAX_DELAY);
}
static bool valid_id(const char *id) {
  size_t n = strlen(id);
  return n > 0 && n < sizeof(((game_info_t *)0)->id) && strspn(id, "abcdefghijklmnopqrstuvwxyz0123456789_-") == n;
}
bool game_store_asset_name(const char *name) {
  size_t n = strlen(name);
  return n > 0 && n < GAME_ASSET_NAME_MAX && name[0] != '.' &&
         strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789_-.") == n;
}
static bool mount_locked(void) {
  if (mounted)
    return true;
  esp_vfs_littlefs_conf_t conf = {
      .base_path = "/littlefs", .partition_label = "littlefs", .format_if_mount_failed = false};
  if (esp_vfs_littlefs_register(&conf) != ESP_OK)
    return false;
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
  if (length < strlen(prefix) || memcmp(source, prefix, strlen(prefix)))
    return false;
  const char *end = memchr(source, '\n', length < 256 ? length : 256);
  if (!end)
    return false;
  char header[256];
  size_t n = end - source - strlen(prefix);
  memcpy(header, source + strlen(prefix), n);
  header[n] = 0;
  cJSON *json = cJSON_ParseWithOpts(header, NULL, true);
  const cJSON *api = cJSON_GetObjectItemCaseSensitive(json, "api");
  const cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "id");
  const cJSON *title = cJSON_GetObjectItemCaseSensitive(json, "title");
  const cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
  const cJSON *needs = cJSON_GetObjectItemCaseSensitive(json, "needs");
  const cJSON *rate = cJSON_GetObjectItemCaseSensitive(json, "rate");
  bool ok = cJSON_IsNumber(api) && api->valuedouble == 1 && cJSON_IsString(id) && valid_id(id->valuestring) &&
            cJSON_IsString(title) && strlen(title->valuestring) > 0 &&
            strlen(title->valuestring) < sizeof(info->title) && cJSON_IsString(version) &&
            strlen(version->valuestring) > 0 && strlen(version->valuestring) < sizeof(info->version);
  // Optional capabilities: a package needing one this host lacks is not listed.
  uint8_t need_bits = 0;
  if (ok && needs) {
    ok = cJSON_IsArray(needs);
    const cJSON *need;
    cJSON_ArrayForEach(need, needs) {
      if (cJSON_IsString(need) && !strcmp(need->valuestring, "imu"))
        need_bits |= GAME_NEEDS_IMU;
      else if (cJSON_IsString(need) && !strcmp(need->valuestring, "warp"))
        need_bits |= GAME_NEEDS_WARP;
      else
        ok = false;
    }
  }
  ok = ok && (!rate || (cJSON_IsNumber(rate) && (rate->valuedouble == 30 || rate->valuedouble == 60)));
  if (ok) {
    strcpy(info->id, id->valuestring);
    strcpy(info->title, title->valuestring);
    strcpy(info->version, version->valuestring);
    info->needs = need_bits;
    info->rate = rate ? (uint8_t)rate->valuedouble : 30;
  }
  cJSON_Delete(json);
  return ok;
}
// Reads a whole file into one PSRAM buffer (plus `extra` zeroed bytes) with a
// single read(). stdio's fread goes through a 128-byte FILE buffer, which made
// littlefs serve an 85 KB asset in hundreds of small reads.
static char *read_file_locked(const char *path, size_t max_size, size_t extra, size_t *length) {
  int fd = open(path, O_RDONLY);
  if (fd < 0)
    return NULL;
  struct stat st;
  char *data = NULL;
  if (!fstat(fd, &st) && st.st_size > 0 && (size_t)st.st_size <= max_size) {
    const size_t size = st.st_size;
    data = heap_caps_malloc(size + extra, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data)
      data = malloc(size + extra);
    size_t done = 0;
    while (data && done < size) {
      const ssize_t n = read(fd, data + done, size - done);
      if (n <= 0)
        break;
      done += n;
    }
    if (data && done == size) {
      memset(data + size, 0, extra);
      *length = size;
    }
    else {
      free(data);
      data = NULL;
    }
  }
  close(fd);
  return data;
}
static char *read_locked(const char *id, size_t *length) {
  if (!valid_id(id) || !mount_locked())
    return NULL;
  char path[96];
  snprintf(path, sizeof(path), ROOT "/%s.lua", id);
  // Sources get a terminating zero.
  return read_file_locked(path, GAME_MAX_BYTES, 1, length);
}
char *game_store_read(const char *id, size_t *length) {
  lock_store();
  char *source = read_locked(id, length);
  xSemaphoreGive(mutex);
  return source;
}
char *game_store_read_asset(const char *id, const char *name, size_t *length) {
  if (!valid_id(id) || !game_store_asset_name(name))
    return NULL;
  lock_store();
  char *data = NULL;
  char path[96];
  snprintf(path, sizeof(path), ROOT "/%s/%s", id, name);
  if (mount_locked())
    data = read_file_locked(path, GAME_ASSET_MAX_BYTES, 0, length);
  xSemaphoreGive(mutex);
  return data;
}
// Removes a package's asset folder; missing folders are fine.
static bool remove_assets_locked(const char *id) {
  char path[96];
  snprintf(path, sizeof(path), ROOT "/%s", id);
  DIR *dir = opendir(path);
  if (!dir)
    return true;
  bool ok = true;
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    char file[sizeof(path) + GAME_ASSET_NAME_MAX + 2];
    snprintf(file, sizeof(file), "%s/%.*s", path, GAME_ASSET_NAME_MAX, entry->d_name);
    ok = unlink(file) == 0 && ok;
  }
  closedir(dir);
  return rmdir(path) == 0 && ok;
}
int game_store_list(game_info_t *games, int capacity) {
  lock_store();
  int count = 0;
  DIR *dir = mount_locked() ? opendir(ROOT) : NULL;
  struct dirent *entry;
  while (dir && count < capacity && (entry = readdir(dir))) {
    size_t n = strlen(entry->d_name);
    if (n < 5 || n >= 28 || strcmp(entry->d_name + n - 4, ".lua"))
      continue;
    char id[24];
    memcpy(id, entry->d_name, n - 4);
    id[n - 4] = 0;
    size_t length;
    char *source = read_locked(id, &length);
    if (source && game_store_metadata(source, length, &games[count]) && !strcmp(id, games[count].id))
      count++;
    free(source);
  }
  if (dir)
    closedir(dir);
  xSemaphoreGive(mutex);
  return count;
}

// One bounded USB upload; staging never replaces the installed file before commit.
static FILE *upload;
static char upload_id[24], upload_asset[GAME_ASSET_NAME_MAX], expected_hash[65];
static size_t expected_size, written;
static psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
static void abort_upload(void) {
  if (upload)
    fclose(upload);
  upload = NULL;
  unlink(ROOT "/.upload");
  psa_hash_abort(&hash);
}
static int hex_digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}
static int game_command(int argc, char **argv) {
  lock_store();
  bool ok = false;
  const char *message = "Invalid command or unformatted LittleFS";
  if (argc == 3 && !strcmp(argv[1], "format") && !strcmp(argv[2], "ERASE_LITTLEFS")) {
    abort_upload();
    ok = esp_littlefs_format("littlefs") == ESP_OK;
    if (ok) {
      ok = mount_locked();
      mkdir(ROOT, 0755);
    }
  }
  else if (mount_locked()) {
    // begin <id> <size> <sha256> [asset name]: the package source, or one of its assets.
    if ((argc == 5 || (argc == 6 && game_store_asset_name(argv[5]))) && !strcmp(argv[1], "begin") &&
        valid_id(argv[2])) {
      abort_upload();
      char *end;
      unsigned long size = strtoul(argv[3], &end, 10);
      const unsigned long limit = argc == 6 ? GAME_ASSET_MAX_BYTES : GAME_MAX_BYTES;
      if (*argv[3] && !*end && size > 0 && size <= limit && strlen(argv[4]) == 64 &&
          strspn(argv[4], "0123456789abcdef") == 64) {
        size_t total, used;
        if (esp_littlefs_info("littlefs", &total, &used) == ESP_OK && total - used > size + 8192) {
          upload = fopen(ROOT "/.upload", "wb");
          if (upload) {
            strcpy(upload_id, argv[2]);
            strcpy(upload_asset, argc == 6 ? argv[5] : "");
            strcpy(expected_hash, argv[4]);
            expected_size = size;
            written = 0;
            ok = psa_hash_setup(&hash, PSA_ALG_SHA_256) == PSA_SUCCESS;
            if (!ok)
              abort_upload();
          }
        }
        else
          message = "Not enough filesystem space";
      }
    }
    else if (argc == 4 && !strcmp(argv[1], "chunk") && upload) {
      char *end;
      unsigned long offset = strtoul(argv[2], &end, 10);
      size_t chars = strlen(argv[3]);
      unsigned char bytes[256];
      ok = *argv[2] && !*end && offset == written && chars > 0 && chars % 2 == 0 && chars <= sizeof(bytes) * 2 &&
           written + chars / 2 <= expected_size;
      for (size_t i = 0; ok && i < chars / 2; i++) {
        int a = hex_digit(argv[3][i * 2]), b = hex_digit(argv[3][i * 2 + 1]);
        if (a < 0 || b < 0)
          ok = false;
        else
          bytes[i] = (a << 4) | b;
      }
      if (ok) {
        ok = fwrite(bytes, 1, chars / 2, upload) == chars / 2;
        if (ok) {
          ok = psa_hash_update(&hash, bytes, chars / 2) == PSA_SUCCESS;
          if (ok)
            written += chars / 2;
        }
      }
      if (!ok)
        abort_upload();
    }
    else if (argc == 2 && !strcmp(argv[1], "commit") && upload && written == expected_size) {
      unsigned char digest[32] = {0};
      char hex[65];
      size_t digest_size = 0;
      ok = psa_hash_finish(&hash, digest, sizeof(digest), &digest_size) == PSA_SUCCESS && digest_size == sizeof(digest);
      for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
      ok = ok && !strcmp(hex, expected_hash) && fflush(upload) == 0;
      int close_result = fclose(upload);
      upload = NULL;
      ok = ok && close_result == 0;
      char path[96];
      if (upload_asset[0]) {
        // Assets are opaque; the package's code validates what it loads.
        snprintf(path, sizeof(path), ROOT "/%s", upload_id);
        mkdir(path, 0755);
        snprintf(path, sizeof(path), ROOT "/%s/%s", upload_id, upload_asset);
      }
      else {
        FILE *file = ok ? fopen(ROOT "/.upload", "rb") : NULL;
        char header[256] = {0};
        game_info_t info;
        if (file) {
          size_t n = fread(header, 1, sizeof(header) - 1, file);
          fclose(file);
          ok = game_store_metadata(header, n, &info) && !strcmp(info.id, upload_id);
        }
        else
          ok = false;
        snprintf(path, sizeof(path), ROOT "/%s.lua", upload_id);
      }
      if (ok)
        ok = rename(ROOT "/.upload", path) == 0;
      abort_upload();
    }
    else if (argc == 3 && !strcmp(argv[1], "remove") && valid_id(argv[2])) {
      char path[96];
      snprintf(path, sizeof(path), ROOT "/%s.lua", argv[2]);
      ok = unlink(path) == 0;
      ok = remove_assets_locked(argv[2]) && ok;
    }
    else if (argc == 2 && !strcmp(argv[1], "abort")) {
      abort_upload();
      ok = true;
    }
  }
  printf("GAME %s%s%s\n", ok ? "OK" : "ERROR", ok ? "" : " ", ok ? "" : message);
  xSemaphoreGive(mutex);
  return ok ? 0 : 1;
}
void game_store_register_console(void) {
  const esp_console_cmd_t cmd = {
      .command = "game", .help = "Install games using scripts/install_game.py", .func = game_command};
  esp_console_cmd_register(&cmd);
}
