#include "vesc_utils.h"
#include <string.h>

uint16_t vesc_crc16(const uint8_t *buf, uint32_t len) {
  uint16_t cksum = 0;
  for (uint32_t i = 0; i < len; i++) {
    cksum ^= ((uint16_t)buf[i] << 8);
    for (int j = 0; j < 8; j++) {
      if (cksum & 0x8000) {
        cksum = (cksum << 1) ^ 0x1021;
      } else {
        cksum <<= 1;
      }
    }
  }
  return cksum;
}

size_t vesc_wrap_packet(const uint8_t *payload, size_t payload_len, uint8_t *out_buf, size_t max_out_len) {
  if (payload_len == 0 || out_buf == NULL) {
    return 0;
  }

  size_t tx_len = 0;
  if (payload_len <= 255) {
    tx_len = payload_len + 5;
    if (tx_len > max_out_len) {
      return 0;
    }
    out_buf[0] = 2;
    out_buf[1] = payload_len;
    memcpy(out_buf + 2, payload, payload_len);
    uint16_t crc = vesc_crc16(payload, payload_len);
    out_buf[payload_len + 2] = crc >> 8;
    out_buf[payload_len + 3] = crc & 0xFF;
    out_buf[payload_len + 4] = 3;
  } else {
    tx_len = payload_len + 6;
    if (tx_len > max_out_len) {
      return 0;
    }
    out_buf[0] = 3;
    out_buf[1] = payload_len >> 8;
    out_buf[2] = payload_len & 0xFF;
    memcpy(out_buf + 3, payload, payload_len);
    uint16_t crc = vesc_crc16(payload, payload_len);
    out_buf[payload_len + 3] = crc >> 8;
    out_buf[payload_len + 4] = crc & 0xFF;
    out_buf[payload_len + 5] = 3;
  }

  return tx_len;
}

int vesc_parse_packet(const uint8_t *stream_buf, size_t stream_len, 
                      uint8_t *out_payload, size_t max_payload_len, 
                      size_t *out_payload_len, size_t *bytes_consumed) {
  *bytes_consumed = 0;
  *out_payload_len = 0;

  if (stream_len == 0) {
    return 0;
  }

  uint8_t start_byte = stream_buf[0];
  if (start_byte != 2 && start_byte != 3 && start_byte != 4) {
    *bytes_consumed = 1;
    return -1;
  }

  size_t header_len = 0;
  uint32_t payload_len = 0;

  if (start_byte == 2) {
    header_len = 2;
    if (stream_len < header_len) {
      return 0;
    }
    payload_len = stream_buf[1];
  } else if (start_byte == 3) {
    header_len = 3;
    if (stream_len < header_len) {
      return 0;
    }
    payload_len = ((uint32_t)stream_buf[1] << 8) | stream_buf[2];
  } else if (start_byte == 4) {
    header_len = 4;
    if (stream_len < header_len) {
      return 0;
    }
    payload_len = ((uint32_t)stream_buf[1] << 16) | 
                  ((uint32_t)stream_buf[2] << 8) | 
                  stream_buf[3];
  }

  size_t total_len = header_len + payload_len + 3;
  if (stream_len < total_len) {
    return 0;
  }

  uint8_t stop_byte = stream_buf[header_len + payload_len + 2];
  if (stop_byte != 3) {
    *bytes_consumed = 1;
    return -1;
  }

  uint16_t rx_crc = ((uint16_t)stream_buf[header_len + payload_len] << 8) |
                    stream_buf[header_len + payload_len + 1];
  uint16_t calc_crc = vesc_crc16(&stream_buf[header_len], payload_len);

  if (rx_crc != calc_crc) {
    *bytes_consumed = 1;
    return -1;
  }

  *bytes_consumed = total_len;
  if (payload_len > max_payload_len) {
    return -2;
  }

  memcpy(out_payload, &stream_buf[header_len], payload_len);
  *out_payload_len = payload_len;
  return 1;
}
