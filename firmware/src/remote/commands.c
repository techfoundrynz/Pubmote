#include "commands.h"
#include "connection.h"
#include "powermanagement.h"
#include "remote/protocol.h"
#include "remote/settings_snapshot.h"
#include "remote/stats.h"
#include "time.h"
#include <esp_log.h>

bool process_board_data(uint8_t *data, int len) {
  ConnectionState state = connection_get_state();
  if (state != CONNECTION_STATE_CONNECTED && state != CONNECTION_STATE_RECONNECTING &&
      state != CONNECTION_STATE_CONNECTING)
    return false;
  BoardTelemetry reading;
  if (len != PROTOCOL_BOARD_DATA_BYTES ||
      !protocol_decode_board(data, (size_t)len, settings_get_pairing().secret_code, &reading)) {
    ESP_LOGD("PUBREMOTE-COMMANDS", "Ignoring invalid board telemetry");
    return false;
  }
  reset_sleep_timer();
  stats_publish_board(&reading, get_current_time_ms());
  stats_update();
  return true;
}
