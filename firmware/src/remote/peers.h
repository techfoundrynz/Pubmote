#ifndef __PEERS_H
#define __PEERS_H
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
  uint8_t mac[6];  // MAC address storage
  char name[32];   // Device name (adjust size as needed)
  uint8_t channel; // Device channel
} SavedPeer;

typedef struct {
  uint8_t deviceCount;
  SavedPeer *devices;
} SavedPeers;


#ifdef __cplusplus
}
#endif

#endif