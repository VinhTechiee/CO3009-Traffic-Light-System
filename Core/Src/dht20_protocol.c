#include "dht20_protocol.h"

/* Aosong DHT20: V1.0 sections 7.3/7.4/8; V1.3 table 8 and section 5.2.
 * See docs/architecture.md#dht20 for references and revision differences.
 */
app_status_t dht20_protocol_crc8(const uint8_t *data, size_t length,
                               uint8_t *crc)
{
    uint8_t remainder = UINT8_C(0xFF);
    size_t byte;
    uint8_t bit;

    if (data == NULL || crc == NULL || length != DHT20_PROTOCOL_PAYLOAD_SIZE) {
        return APP_INVALID_ARGUMENT;
    }

    for (byte = 0u; byte < DHT20_PROTOCOL_PAYLOAD_SIZE; ++byte) {
        remainder ^= data[byte];
        for (bit = 0u; bit < 8u; ++bit) {
            if ((remainder & UINT8_C(0x80)) != 0u) {
                remainder = (uint8_t)((remainder << 1) ^ UINT8_C(0x31));
            } else {
                remainder = (uint8_t)(remainder << 1);
            }
        }
    }
    *crc = remainder;
    return APP_OK;
}

app_status_t dht20_protocol_parse_frame(const uint8_t *frame, size_t length,
                                      dht20_sample_t *sample)
{
    uint8_t crc;
    uint32_t humidity_raw;
    uint32_t temperature_raw;
    dht20_sample_t parsed;

    if (frame == NULL || sample == NULL || length != DHT20_PROTOCOL_FRAME_SIZE) {
        return APP_INVALID_ARGUMENT;
    }

    (void)dht20_protocol_crc8(frame, DHT20_PROTOCOL_PAYLOAD_SIZE, &crc);
    if (crc != frame[DHT20_PROTOCOL_PAYLOAD_SIZE]) {
        return APP_IO_ERROR;
    }
    if ((frame[0] & UINT8_C(0x80)) != 0u) {
        return APP_BUSY;
    }
    if ((frame[0] & UINT8_C(0x10)) == 0u) {
        return APP_IO_ERROR;
    }
    if ((frame[0] & UINT8_C(0x08)) == 0u) {
        return APP_NOT_READY;
    }

    humidity_raw = ((uint32_t)frame[1] << 12)
                 | ((uint32_t)frame[2] << 4)
                 | ((uint32_t)frame[3] >> 4);
    temperature_raw = ((uint32_t)(frame[3] & UINT8_C(0x0F)) << 16)
                    | ((uint32_t)frame[4] << 8)
                    | (uint32_t)frame[5];

    parsed.relative_humidity_percent = (float)humidity_raw * (100.0f / 1048576.0f);
    parsed.temperature_c = (float)temperature_raw * (200.0f / 1048576.0f) - 50.0f;
    parsed.valid = true;
    *sample = parsed;
    return APP_OK;
}
