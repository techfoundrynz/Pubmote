#ifndef __BOARDS_SCREEN_H
#define __BOARDS_SCREEN_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  void setup_boards_properties();
  void handle_select_board(int idx);
  void handle_delete_board(int idx);
  void handle_boards_back();
  void handle_boards_pair_new();
  void teardown_boards_properties();

#ifdef __cplusplus
}
#endif

#endif
