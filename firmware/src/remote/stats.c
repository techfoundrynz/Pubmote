#include "stats.h"
#include "receiver.h"
#include <screens/stats_screen.h>

RemoteStats remoteStats = {.signalStrength = -255, .state = BOARD_STATE_BOOT};

static callback_registry_t stats_update_registry = {0};

void stats_update() {
  registry_cb(&stats_update_registry, false);
}

static void reset_stats() {
  remoteStats.lastUpdated = 0;
  remoteStats.speed = 0.0;
  remoteStats.dutyCycle = 0;
  remoteStats.speedUnit = SPEED_UNIT_KMH;
  remoteStats.tempUnit = TEMP_UNIT_CELSIUS;
  remoteStats.batteryVoltage = 0.0;
  remoteStats.batteryPercentage = 0.0;
  remoteStats.tripDistance = 0.0;
  remoteStats.motorTemp = 0;
  remoteStats.controllerTemp = 0;
  remoteStats.signalStrength = -255;
  remoteStats.state = BOARD_STATE_STARTUP;
  remoteStats.switchState = SWITCH_STATE_OFF;
  remoteStats.chargeState = CHARGE_STATE_UNKNOWN;
  remoteStats.chargeCurrent = 0;

  stats_update();
}

void stats_init() {
  reset_stats();
}

void stats_register_update_cb(callback_t callback) {
  // Register the callback for stats updates
  register_cb(&stats_update_registry, callback);
}

void stats_unregister_update_cb(callback_t callback) {
  // Unregister the callback for stats updates
  remove_cb(&stats_update_registry, callback);
}
