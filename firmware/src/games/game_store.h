#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GAME_MAX_BYTES 65536
#define GAME_MAX_INSTALLED 12
typedef struct {
  char id[24];
  char title[48];
  char version[24];
} game_info_t;
bool game_store_mount(void);
int game_store_list(game_info_t *games, int capacity);
char *game_store_read(const char *id, size_t *length);
bool game_store_metadata(const char *source, size_t length, game_info_t *info);
void game_store_register_console(void);
#ifdef __cplusplus
}
#endif
