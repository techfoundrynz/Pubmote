#pragma once
#include "remote/input_types.h"
#include "remote/telemetry_types.h"
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
  typedef enum {
    REM_VERSION = 0,
    REM_RECEIVER_VERSION = 5,
    REM_PAIR_INIT = 10,
    REM_PAIR_BOND = 11,
    REM_PAIR_COMPLETE = 12,
    REM_SET_CORE_DATA = 100,
    REM_SET_INPUT_STATE = 150,
  } RemoteCommands;

#define PROTOCOL_BOARD_DATA_BYTES 32
#define PROTOCOL_INPUT_PACKET_BYTES 17
#define PROTOCOL_VERSION_PACKET_BYTES 8
#define PROTOCOL_VERSION_REQUEST_BYTES 5
#define PROTOCOL_PAIR_RESPONSE_BYTES 2

  typedef struct {
    uint16_t api_version;
    bool has_vehicle_type;
    uint8_t vehicle_type;
  } ReceiverVersion;

  bool protocol_decode_board(const uint8_t *data, size_t length, uint32_t expected_secret, BoardTelemetry *out);
  bool protocol_decode_receiver_version(const uint8_t *data, size_t length, ReceiverVersion *out);
  bool protocol_decode_pair_init(const uint8_t *data, size_t length, const uint8_t sender[6], bool ble,
                                 uint8_t heard_channel, uint8_t *channel);
  bool protocol_decode_pair_secret(const uint8_t *data, size_t length, uint32_t *secret);
  bool protocol_decode_pair_complete(const uint8_t *data, size_t length, bool *accepted);
  size_t protocol_encode_input(uint8_t *out, size_t capacity, uint32_t secret, const RemoteData *input);
  size_t protocol_encode_version(uint8_t *out, size_t capacity, uint32_t secret, const uint8_t version[3]);
  size_t protocol_encode_version_request(uint8_t *out, size_t capacity, uint32_t secret);
  size_t protocol_encode_pair_response(uint8_t *out, size_t capacity);
#ifdef __cplusplus
}
#endif
