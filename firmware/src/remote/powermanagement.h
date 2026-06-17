#ifndef __POWERMANAGEMENT_H
#define __POWERMANAGEMENT_H
#include <stdbool.h>
#include <stdint.h>



#ifdef __cplusplus
extern "C" {
#endif


void acc1_power_set_level(bool enable);
void acc2_power_set_level(uint8_t level);
void reset_sleep_timer();
void power_management_init();
void enter_sleep();

typedef enum {
  CHARGING,
  ON,
  OFF,
} PowerState;



#ifdef __cplusplus
}
#endif

#endif