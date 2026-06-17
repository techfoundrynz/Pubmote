#ifndef __CALLBACK_REGISTRY_H
#define __CALLBACK_REGISTRY_H
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef void (*callback_t)(void);

typedef struct {
  callback_t *callbacks;
  size_t count;
  size_t capacity;
} callback_registry_t;

void register_cb(callback_registry_t *registry, callback_t callback);
void remove_cb(callback_registry_t *registry, callback_t callback);
void registry_cb(callback_registry_t *registry, bool cleanup);



#ifdef __cplusplus
}
#endif

#endif