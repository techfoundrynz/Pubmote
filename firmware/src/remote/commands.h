#ifndef __COMMANDS_H
#define __COMMANDS_H

#include <stdbool.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
  // Receiver version commands
  REM_VERSION = 0,
  // Receiver version commands
  REM_RECEIVER_VERSION = 5,
  // Pairing commands
  REM_PAIR_INIT = 10,
  REM_PAIR_BOND = 11,
  REM_PAIR_COMPLETE = 12,
  // Remote specific commands
  REM_SET_CORE_DATA = 100,
  // Receiver specific commands
  REM_SET_INPUT_STATE = 150,
} RemoteCommands;

bool process_board_data(uint8_t *data, int len);



#ifdef __cplusplus
}
#endif

#endif