#include "remote/stats.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static RemoteStats state = {.signalStrength = -255, .state = BOARD_STATE_BOOT};
#define MAX_STATS_CALLBACKS 16
static stats_update_callback_t callbacks[MAX_STATS_CALLBACKS];

RemoteStats stats_snapshot(void) {
  portENTER_CRITICAL(&state_lock);
  RemoteStats snapshot = state;
  portEXIT_CRITICAL(&state_lock);
  return snapshot;
}

void stats_publish_board(const BoardTelemetry *reading, int64_t timestamp) {
  if (!reading)
    return;
  portENTER_CRITICAL(&state_lock);
  state.lastUpdated = timestamp;
  state.speed = reading->speed;
  state.dutyCycle = reading->dutyCycle;
  state.phaseUtilization = reading->phaseUtilization;
  state.batteryUtilization = reading->batteryUtilization;
  state.speedUnit = SPEED_UNIT_KMH;
  state.tempUnit = TEMP_UNIT_CELSIUS;
  state.batteryVoltage = reading->batteryVoltage;
  state.batteryPercentage = reading->batteryPercentage;
  state.tripDistance = reading->tripDistance;
  state.motorTemp = reading->motorTemp;
  state.controllerTemp = reading->controllerTemp;
  state.state = reading->state;
  state.switchState = reading->switchState;
  portEXIT_CRITICAL(&state_lock);
}

void stats_publish_power(uint16_t voltage, uint8_t percentage, RemoteChargeState charge_state, uint16_t current) {
  portENTER_CRITICAL(&state_lock);
  state.remoteBatteryVoltage = voltage;
  state.remoteBatteryPercentage = percentage;
  state.chargeState = charge_state;
  state.chargeCurrent = current;
  portEXIT_CRITICAL(&state_lock);
}

void stats_set_signal_strength(int rssi) {
  portENTER_CRITICAL(&state_lock);
  state.signalStrength = rssi;
  portEXIT_CRITICAL(&state_lock);
}
void stats_set_vehicle_type(uint8_t vehicle_type) {
  portENTER_CRITICAL(&state_lock);
  state.vehicleType = vehicle_type;
  portEXIT_CRITICAL(&state_lock);
}
uint8_t stats_utilization(const RemoteStats *stats) {
  int best = stats->dutyCycle;
  const int terms[] = {stats->phaseUtilization, stats->batteryUtilization};
  for (unsigned i = 0; i < sizeof(terms) / sizeof(terms[0]); ++i) {
    int mag = terms[i] < 0 ? -terms[i] : terms[i];
    if (mag > best)
      best = mag;
  }
  return (uint8_t)(best > 100 ? 100 : best);
}

void stats_set_duty_cycle(uint8_t duty_cycle) {
  portENTER_CRITICAL(&state_lock);
  state.dutyCycle = duty_cycle;
  portEXIT_CRITICAL(&state_lock);
}

void stats_reset(uint8_t vehicle_type) {
  portENTER_CRITICAL(&state_lock);
  RemoteStats reset = {.maxSpeed = state.maxSpeed,
                       .remoteBatteryVoltage = state.remoteBatteryVoltage,
                       .remoteBatteryPercentage = state.remoteBatteryPercentage,
                       .signalStrength = -255,
                       .state = BOARD_STATE_STARTUP,
                       .chargeState = CHARGE_STATE_UNKNOWN,
                       .vehicleType = vehicle_type};
  state = reset;
  portEXIT_CRITICAL(&state_lock);
}

void stats_update(void) {
  stats_update_callback_t pending[MAX_STATS_CALLBACKS];
  portENTER_CRITICAL(&state_lock);
  memcpy(pending, callbacks, sizeof(pending));
  portEXIT_CRITICAL(&state_lock);
  // Callbacks can read/publish/register again; never invoke them under the lock.
  for (unsigned i = 0; i < MAX_STATS_CALLBACKS; ++i) {
    if (pending[i])
      pending[i]();
  }
}
bool stats_register_update_cb(stats_update_callback_t callback) {
  if (!callback)
    return false;
  portENTER_CRITICAL(&state_lock);
  unsigned free_slot = MAX_STATS_CALLBACKS;
  bool registered = false;
  for (unsigned i = 0; i < MAX_STATS_CALLBACKS; ++i) {
    if (callbacks[i] == callback) {
      registered = true;
      break;
    }
    if (!callbacks[i] && free_slot == MAX_STATS_CALLBACKS)
      free_slot = i;
  }
  if (!registered && free_slot < MAX_STATS_CALLBACKS) {
    callbacks[free_slot] = callback;
    registered = true;
  }
  portEXIT_CRITICAL(&state_lock);
  return registered;
}
void stats_unregister_update_cb(stats_update_callback_t callback) {
  portENTER_CRITICAL(&state_lock);
  for (unsigned i = 0; i < MAX_STATS_CALLBACKS; ++i) {
    if (callbacks[i] == callback)
      callbacks[i] = NULL;
  }
  portEXIT_CRITICAL(&state_lock);
}
