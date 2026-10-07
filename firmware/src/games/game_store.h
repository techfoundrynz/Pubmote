#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
#define GAME_MAX_BYTES 131072
#define GAME_MAX_INSTALLED 12
// Package asset files live in /games/<id>/<name>; names are 1-31 of a-z 0-9 _ - .
// and do not start with a dot.
#define GAME_ASSET_MAX_BYTES 262144
#define GAME_ASSET_NAME_MAX 32
// Optional host capabilities a package declares with "needs".
#define GAME_NEEDS_IMU 0x01
#define GAME_NEEDS_WARP 0x02
  typedef struct {
    char id[24];
    char title[48];
    char version[24];
    uint8_t needs; // GAME_NEEDS_* bits
    uint8_t rate;  // update callbacks per second: 30 (default) or 60
  } game_info_t;
  bool game_store_mount(void);
  int game_store_list(game_info_t *games, int capacity);
  char *game_store_read(const char *id, size_t *length);
  // Reads one asset file of an installed package into PSRAM; free() it.
  char *game_store_read_asset(const char *id, const char *name, size_t *length);
  bool game_store_asset_name(const char *name);
  bool game_store_metadata(const char *source, size_t length, game_info_t *info);
  void game_store_register_console(void);
#ifdef __cplusplus
}
#endif
