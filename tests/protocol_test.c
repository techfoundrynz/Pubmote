#include "remote/protocol.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static void test_encoding(void) {
  uint8_t out[32];
  RemoteData input = {.js_y = -1.0f, .js_x = 0.5f, .bt_c = true, .bt_z = false, .is_rev = true};
  const uint8_t golden[] = {150, 0x67, 0x45, 0x23, 0x81, 0, 0, 0x80, 0xbf, 0, 0, 0, 0x3f, 1, 0, 1, 0};
  assert(protocol_encode_input(out, sizeof(out), 0x81234567, &input) == sizeof(golden));
  assert(!memcmp(out, golden, sizeof(golden)));
  uint8_t version[] = {1, 2, 3};
  const uint8_t version_golden[] = {0, 0x67, 0x45, 0x23, 0x81, 1, 2, 3};
  assert(protocol_encode_version(out, sizeof(out), 0x81234567, version) == sizeof(version_golden));
  assert(!memcmp(out, version_golden, sizeof(version_golden)));
  assert(protocol_encode_version_request(out, sizeof(out), 0x81234567) == 5);
  assert(out[0] == REM_RECEIVER_VERSION && !memcmp(out + 1, golden + 1, 4));
  assert(protocol_encode_pair_response(out, sizeof(out)) == 2 && out[0] == 11 && out[1] == 0);
  for (size_t capacity = 0; capacity < PROTOCOL_INPUT_PACKET_BYTES; ++capacity) {
    memset(out, 0xa5, sizeof(out));
    assert(!protocol_encode_input(out, capacity, 1, &input));
    for (size_t i = 0; i < sizeof(out); ++i)
      assert(out[i] == 0xa5);
  }
  assert(!protocol_encode_input(NULL, 32, 1, &input));
  assert(!protocol_encode_input(out, 32, 1, NULL));
  assert(!protocol_encode_version(out, 32, 1, NULL));
}
static void test_decoding(void) {
  uint8_t packet[32] = {0x81, 0x23, 0x45, 0x67};
  BoardTelemetry out = {.speed = 99};
  assert(!protocol_decode_board(packet, 31, 0x81234567, &out) && out.speed == 99);
  assert(!protocol_decode_board(packet, 32, 1, &out) && out.speed == 99);
  assert(!protocol_decode_board(NULL, 32, 1, &out));
  assert(!protocol_decode_board(packet, 32, 0x81234567, NULL));
  // Exhaustive duty bytes and all signed speed values preserve legacy rounding.
  for (unsigned i = 0; i < 256; ++i) {
    packet[19] = (uint8_t)i;
    assert(protocol_decode_board(packet, 32, 0x81234567, &out));
    float old_duty = (float)i / 100.0 - 0.5;
    assert(out.dutyCycle == (uint8_t)(fabs(old_duty) * 100));
  }
  for (int speed = -32768; speed <= 32767; ++speed) {
    packet[15] = (uint8_t)((uint16_t)speed >> 8);
    packet[16] = (uint8_t)speed;
    assert(protocol_decode_board(packet, 32, 0x81234567, &out));
    float old_speed = (int16_t)speed / 10.0;
    assert(out.speed == (float)(fabs(old_speed) * 3.6));
  }
  const uint8_t version[] = {0, 0, 0, 0, 0x34, 0x12, 4};
  ReceiverVersion receiver = {0};
  assert(!protocol_decode_receiver_version(version, 4, &receiver));
  assert(protocol_decode_receiver_version(version, 5, &receiver) && receiver.api_version == 0x34 &&
         !receiver.has_vehicle_type);
  assert(protocol_decode_receiver_version(version, 6, &receiver) && receiver.api_version == 0x1234 &&
         !receiver.has_vehicle_type);
  assert(protocol_decode_receiver_version(version, 7, &receiver) && receiver.has_vehicle_type &&
         receiver.vehicle_type == 4);
  const uint8_t sender[] = {1, 2, 3, 4, 5, 6};
  uint8_t pair[] = {1, 2, 3, 4, 5, 6, 11};
  uint8_t channel = 99;
  assert(protocol_decode_pair_init(pair, sizeof(pair), sender, false, 6, &channel) && channel == 11);
  pair[6] = 0;
  assert(protocol_decode_pair_init(pair, sizeof(pair), sender, false, 6, &channel) && channel == 6);
  pair[0] = 9;
  channel = 99;
  assert(!protocol_decode_pair_init(pair, sizeof(pair), sender, false, 6, &channel) && channel == 99);
  assert(protocol_decode_pair_init(pair, sizeof(pair), sender, true, 6, &channel) && channel == 0x86);
  uint32_t secret = 99;
  assert(!protocol_decode_pair_secret(packet, 3, &secret) && secret == 99);
  assert(protocol_decode_pair_secret(packet, 4, &secret) && secret == 0x81234567);
  bool accepted = false;
  pair[0] = 1;
  assert(protocol_decode_pair_complete(pair, 1, &accepted) && accepted);
  pair[0] = 2;
  assert(protocol_decode_pair_complete(pair, 1, &accepted) && !accepted);
  assert(!protocol_decode_pair_complete(pair, 2, &accepted));
}
int main(void) {
  test_encoding();
  test_decoding();
}
