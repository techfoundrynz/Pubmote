#include "remote/protocol.h"
#include "remote/settings_state.h"
#include "remote/stats.h"
#include <assert.h>
#include <math.h>
#include <string.h>

typedef enum {
  CONNECTION_STATE_DISCONNECTED,
  CONNECTION_STATE_CONNECTED,
  CONNECTION_STATE_RECONNECTING,
  CONNECTION_STATE_CONNECTING
} ConnectionState;
static int connection_state = CONNECTION_STATE_CONNECTED;
static int sleep_resets, notifications;
ConnectionState connection_get_state(void) {
  return connection_state;
}
int64_t get_current_time_ms(void) {
  return 1234567890123LL;
}
void reset_sleep_timer(void) {
  ++sleep_resets;
}
static void notified(void) {
  ++notifications;
}
#include "commands/commands.c"

int main(void) {
  SettingsSnapshot initial = {0};
  initial.pairing.default_index = -1;
  initial.pairing.secret_code = 0x81234567;
  settings_state_init(&initial);
  stats_reset(VEHICLE_TYPE_ESKATE);
  stats_publish_power(4200, 99, CHARGE_STATE_DONE, 123);
  stats_set_signal_strength(-60);
  stats_register_update_cb(notified);
  uint8_t packet[33] = {0x81, 0x23, 0x45, 0x67};
  packet[9] = BOARD_STATE_RUNNING;
  packet[10] = SWITCH_STATE_BOTH;
  packet[11] = 2;
  packet[12] = 128; // 64.0 volts, big endian tenths
  packet[15] = 0xff;
  packet[16] = 0x9c; // -10.0 m/s
  packet[19] = 75;   // 25% duty
  packet[20] = 60;   // 60% phase utilization
  packet[21] = (uint8_t)-30; // 30% regen
  float distance = 1234.5f;
  memcpy(packet + 22, &distance, sizeof(distance));
  packet[26] = 70;
  packet[27] = 80;
  packet[32] = 160;
  assert(process_board_data(packet, sizeof(packet)));
  RemoteStats value = stats_snapshot();
  assert(value.lastUpdated == 1234567890123LL && fabsf(value.speed - 36.0f) < 0.001f);
  assert(value.batteryVoltage == 64.0f && value.batteryPercentage == 80 && value.dutyCycle == 25);
  assert(value.phaseUtilization == 60 && value.batteryUtilization == -30);
  assert(stats_utilization(&value) == 60);
  RemoteStats util = {.dutyCycle = 40, .phaseUtilization = -55, .batteryUtilization = 20};
  assert(stats_utilization(&util) == 55); // braking counts by magnitude
  util.batteryUtilization = 70;
  assert(stats_utilization(&util) == 70);
  util = (RemoteStats){.dutyCycle = 90, .phaseUtilization = -50};
  assert(stats_utilization(&util) == 90);
  assert(value.controllerTemp == 35 && value.motorTemp == 40 && value.tripDistance == distance);
  assert(value.state == BOARD_STATE_RUNNING && value.switchState == SWITCH_STATE_BOTH);
  assert(value.vehicleType == VEHICLE_TYPE_ESKATE && value.remoteBatteryVoltage == 4200);
  assert(value.chargeCurrent == 123 && value.signalStrength == -60);
  assert(sleep_resets == 1 && notifications == 1);
  packet[0] ^= 1;
  assert(!process_board_data(packet, sizeof(packet)));
  assert(!process_board_data(packet, 32));
  assert(!process_board_data(NULL, 33));
  packet[0] ^= 1;
  connection_state = CONNECTION_STATE_DISCONNECTED;
  assert(!process_board_data(packet, sizeof(packet)));
  RemoteStats unchanged = stats_snapshot();
  assert(!memcmp(&value, &unchanged, sizeof(value)));
  assert(sleep_resets == 1 && notifications == 1);
  return 0;
}
