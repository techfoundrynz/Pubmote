#ifndef __VESC_UTILS_H
#define __VESC_UTILS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t vesc_crc16(const uint8_t *buf, uint32_t len);

/**
 * Wraps payload into a VESC packet.
 * out_buf must be large enough (at least payload_len + 6).
 * Returns the total wrapped packet length, or 0 on error.
 */
size_t vesc_wrap_packet(const uint8_t *payload, size_t payload_len, uint8_t *out_buf, size_t max_out_len);

/**
 * Tries to parse a single VESC packet from a stream buffer.
 *
 * @param stream_buf Input bytes stream
 * @param stream_len Number of bytes in stream_buf
 * @param out_payload Buffer to copy payload to
 * @param max_payload_len Capacity of out_payload
 * @param out_payload_len Receives the decoded payload length
 * @param bytes_consumed Receives the number of bytes consumed from stream_buf (always > 0 if returning < 0)
 *
 * @return 
 *   1: Valid packet parsed successfully
 *   0: Incomplete packet, need more bytes
 *  -1: Invalid packet header/stop-byte/checksum (bytes_consumed set to 1)
 *  -2: Output buffer too small
 */
int vesc_parse_packet(const uint8_t *stream_buf, size_t stream_len, 
                      uint8_t *out_payload, size_t max_payload_len, 
                      size_t *out_payload_len, size_t *bytes_consumed);

#ifdef __cplusplus
}
#endif

#endif
