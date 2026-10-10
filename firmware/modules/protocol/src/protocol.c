#include "remote/protocol.h"
#include <float.h>
#include <math.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24, "Wire format requires IEEE binary32");
static uint32_t read_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static int16_t read_be16(const uint8_t *p) {
  return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t read_le32(const uint8_t *p) {
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void write_le32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (uint8_t)(value >> (8 * i));
}
static void write_float(uint8_t *p, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  write_le32(p, bits);
}
static int8_t clamp_pct(int8_t v) {
  return v > 100 ? 100 : (v < -100 ? -100 : v);
}
bool protocol_decode_board(const uint8_t *data, size_t length, uint32_t expected_secret, BoardTelemetry *out) {
  if (!data || !out || length != PROTOCOL_BOARD_DATA_BYTES || read_be32(data) != expected_secret)
    return false;
  BoardTelemetry result = {0};
  float speed = read_be16(data + 15) / 10.0;
  result.speed = (float)(fabs(speed) * 3.6);
  result.batteryVoltage = read_be16(data + 11) / 10.0;
  result.batteryPercentage = (uint8_t)((float)data[32] / 2.0);
  float duty = (float)data[19] / 100.0 - 0.5;
  result.dutyCycle = (uint8_t)(fabs(duty) * 100);
  result.phaseUtilization = clamp_pct((int8_t)data[20]);
  result.batteryUtilization = clamp_pct((int8_t)data[21]);
  result.motorTemp = (float)data[27] / 2.0;
  result.controllerTemp = (float)data[26] / 2.0;
  result.state = (BoardState)data[9];
  result.switchState = (SwitchState)data[10];
  uint32_t distance = read_le32(data + 22);
  memcpy(&result.tripDistance, &distance, sizeof(distance));
  *out = result;
  return true;
}
bool protocol_decode_receiver_version(const uint8_t *data, size_t length, ReceiverVersion *out) {
  if (!data || !out || length < 5)
    return false;
  ReceiverVersion result = {.api_version = data[4], .has_vehicle_type = length >= 7};
  if (length >= 6)
    result.api_version |= (uint16_t)data[5] << 8;
  if (result.has_vehicle_type)
    result.vehicle_type = data[6];
  *out = result;
  return true;
}
bool protocol_decode_pair_init(const uint8_t *data, size_t length, const uint8_t sender[6], bool ble,
                               uint8_t heard_channel, uint8_t *channel) {
  if (!data || !sender || !channel || length != 7 || (!ble && memcmp(data, sender, 6)))
    return false;
  *channel = ble ? (heard_channel | 0x80) : ((data[6] >= 1 && data[6] <= 14) ? data[6] : heard_channel);
  return true;
}
bool protocol_decode_pair_secret(const uint8_t *data, size_t length, uint32_t *secret) {
  if (!data || !secret || length != 4)
    return false;
  *secret = read_be32(data);
  return true;
}
bool protocol_decode_pair_complete(const uint8_t *data, size_t length, bool *accepted) {
  if (!data || !accepted || length != 1)
    return false;
  *accepted = data[0] == 1;
  return true;
}
static size_t command(uint8_t *out, size_t capacity, uint8_t id, uint32_t secret, size_t length) {
  if (!out || capacity < length)
    return 0;
  out[0] = id;
  write_le32(out + 1, secret);
  return length;
}
size_t protocol_encode_input(uint8_t *out, size_t capacity, uint32_t secret, const RemoteData *input) {
  if (!input || !command(out, capacity, REM_SET_INPUT_STATE, secret, PROTOCOL_INPUT_PACKET_BYTES))
    return 0;
  write_float(out + 5, input->js_y);
  write_float(out + 9, input->js_x);
  out[13] = input->bt_c;
  out[14] = input->bt_z;
  out[15] = input->is_rev;
  out[16] = 0;
  return PROTOCOL_INPUT_PACKET_BYTES;
}
size_t protocol_encode_version(uint8_t *out, size_t capacity, uint32_t secret, const uint8_t version[3]) {
  if (!version || !command(out, capacity, REM_VERSION, secret, PROTOCOL_VERSION_PACKET_BYTES))
    return 0;
  memcpy(out + 5, version, 3);
  return PROTOCOL_VERSION_PACKET_BYTES;
}
size_t protocol_encode_version_request(uint8_t *out, size_t capacity, uint32_t secret) {
  return command(out, capacity, REM_RECEIVER_VERSION, secret, PROTOCOL_VERSION_REQUEST_BYTES);
}
size_t protocol_encode_pair_response(uint8_t *out, size_t capacity) {
  if (!out || capacity < PROTOCOL_PAIR_RESPONSE_BYTES)
    return 0;
  out[0] = REM_PAIR_BOND;
  out[1] = 0;
  return PROTOCOL_PAIR_RESPONSE_BYTES;
}
