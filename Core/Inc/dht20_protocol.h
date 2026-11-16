#ifndef CO3009_DHT20_PROTOCOL_H
#define CO3009_DHT20_PROTOCOL_H

#include <stddef.h>
#include "dht20.h"

#define DHT20_PROTOCOL_FRAME_SIZE 7u
#define DHT20_PROTOCOL_PAYLOAD_SIZE 6u

/* CRC-8 over exactly six bytes (status + packed measurements).
 * Poly 0x31, init 0xFF, MSB first, no reflection or final XOR.
 * NULL or a length other than six: APP_INVALID_ARGUMENT.
 * Output is unchanged on failure. See docs/dht20-protocol.md for sources.
 */
app_status_t dht20_protocol_crc8(const uint8_t *data, size_t length,
                               uint8_t *crc);

/* Parse exactly seven bytes, including mandatory CRC; no bus address byte.
 * Order: arguments/length, CRC, busy, sensor status, conversion.
 * APP_IO_ERROR: CRC mismatch or OTP integrity flag (bit 4) clear.
 * APP_BUSY: measurement in progress (bit 7 set).
 * APP_NOT_READY: calibration disabled (bit 3 clear).
 * Other status bits are not rejected. Output is unchanged on any failure.
 * APP_OK sets valid=true; temperature_c is degrees Celsius and humidity is
 * percent RH. Full 20-bit encoding is converted without application clamping;
 * protocol validity does not establish operating range or sample freshness.
 */
app_status_t dht20_protocol_parse_frame(const uint8_t *frame, size_t length,
                                      dht20_sample_t *sample);

#endif /* CO3009_DHT20_PROTOCOL_H */
