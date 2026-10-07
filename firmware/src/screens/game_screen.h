#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C"
{
#endif
  void setup_game_properties(void);
  void teardown_game_properties(void);
  bool game_screen_prepare_exit(int destination);
  void handle_game_tick(void);
  void handle_game_event(int kind, float x, float y);
  void handle_game_launch(int index);
  void handle_game_back(void);
  void game_refresh_catalog(void);
  void handle_game_placed(bool placed);
#ifdef __cplusplus
}
#endif
