#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dht20_protocol.h"

/* Independent polynomial long division, retaining the ninth bit explicitly. */
static uint8_t reference_crc(const uint8_t data[6])
{
    uint16_t remainder = UINT16_C(0xFF);
    size_t i;
    unsigned int bit;
    for (i = 0u; i < 6u; ++i) {
        remainder ^= data[i];
        remainder = (uint16_t)(remainder << 8);
        for (bit = 0u; bit < 8u; ++bit) {
            const uint16_t divisor = (uint16_t)(UINT16_C(0x131) << (7u - bit));
            if ((remainder & (uint16_t)(UINT16_C(0x8000) >> bit)) != 0u) {
                remainder ^= divisor;
            }
        }
    }
    return (uint8_t)remainder;
}

static void make_frame(uint8_t frame[7], uint8_t status,
                       uint32_t humidity, uint32_t temperature)
{
    assert(humidity <= UINT32_C(0xFFFFF));
    assert(temperature <= UINT32_C(0xFFFFF));
    frame[0] = status;
    frame[1] = (uint8_t)(humidity >> 12);
    frame[2] = (uint8_t)(humidity >> 4);
    frame[3] = (uint8_t)(((humidity & 15u) << 4) | (temperature >> 16));
    frame[4] = (uint8_t)(temperature >> 8);
    frame[5] = (uint8_t)temperature;
    frame[6] = reference_crc(frame);
}

static void expect_failure(const uint8_t *frame, size_t length,
                           app_status_t expected)
{
    unsigned int initially_valid;
    for (initially_valid = 0u; initially_valid < 2u; ++initially_valid) {
        dht20_sample_t sample;
        unsigned char before[sizeof sample];
        /* Initialize padding too, then snapshot the complete object bytes. */
        memset(&sample, 0, sizeof sample);
        sample.temperature_c = 12.0f;
        sample.relative_humidity_percent = 34.0f;
        sample.valid = initially_valid != 0u;
        memcpy(before, &sample, sizeof sample);
        assert(dht20_protocol_parse_frame(frame, length, &sample) == expected);
        assert(sample.temperature_c == 12.0f);
        assert(sample.relative_humidity_percent == 34.0f);
        assert(sample.valid == (initially_valid != 0u));
        assert(memcmp(before, &sample, sizeof sample) == 0);
    }
}

static void test_valid_and_crc(void)
{
    const uint8_t known[7] = {0x18u, 0x80u, 0x00u, 0x06u, 0x00u, 0x00u, 0x23u};
    uint8_t changed[7];
    uint8_t crc = 0u;
    unsigned int bit;
    dht20_sample_t sample;
    assert(reference_crc(known) == 0x23u);
    assert(dht20_protocol_crc8(known, 6u, &crc) == APP_OK);
    assert(crc == 0x23u);
    assert(dht20_protocol_parse_frame(known, 7u, &sample) == APP_OK);
    assert(sample.valid);
    assert(sample.temperature_c == 25.0f);
    assert(sample.relative_humidity_percent == 50.0f);
    for (bit = 0u; bit < 56u; ++bit) {
        memcpy(changed, known, sizeof changed);
        changed[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        expect_failure(changed, 7u, APP_IO_ERROR);
    }
}

static void test_status(void)
{
    uint8_t frame[7];
    unsigned int status;
    for (status = 0u; status <= 255u; ++status) {
        uint8_t crc;
        dht20_sample_t sample;
        app_status_t expected;
        make_frame(frame, (uint8_t)status, 0u, 0u);
        assert(dht20_protocol_crc8(frame, 6u, &crc) == APP_OK);
        assert(crc == frame[6]);
        if ((status & 0x80u) != 0u) {
            expected = APP_BUSY;
        } else if ((status & 0x10u) == 0u) {
            expected = APP_IO_ERROR;
        } else if ((status & 0x08u) == 0u) {
            expected = APP_NOT_READY;
        } else {
            expected = APP_OK;
        }
        if (expected == APP_OK) {
            assert(dht20_protocol_parse_frame(frame, 7u, &sample) == APP_OK);
            assert(sample.valid);
        } else {
            expect_failure(frame, 7u, expected);
        }
    }
}

static void test_arguments(void)
{
    uint8_t frame[8] = {0u};
    uint8_t crc = 0x55u;
    size_t length;
    make_frame(frame, 0x18u, 0u, 0u);
    expect_failure(NULL, 7u, APP_INVALID_ARGUMENT);
    assert(dht20_protocol_parse_frame(frame, 7u, NULL) == APP_INVALID_ARGUMENT);
    for (length = 0u; length <= 8u; ++length) {
        if (length != 7u) {
            expect_failure(frame, length, APP_INVALID_ARGUMENT);
        }
        if (length != 6u) {
            assert(dht20_protocol_crc8(frame, length, &crc) == APP_INVALID_ARGUMENT);
            assert(crc == 0x55u);
        }
    }
    expect_failure(frame, (size_t)-1, APP_INVALID_ARGUMENT);
    assert(dht20_protocol_crc8(NULL, 6u, &crc) == APP_INVALID_ARGUMENT);
    assert(crc == 0x55u);
    assert(dht20_protocol_crc8(frame, 6u, NULL) == APP_INVALID_ARGUMENT);
    assert(dht20_protocol_crc8(frame, (size_t)-1, &crc) == APP_INVALID_ARGUMENT);
    assert(crc == 0x55u);
}

static void assert_close(float actual, double expected)
{
    const double difference = (double)actual - expected;
    assert(difference < 0.00002 && difference > -0.00002);
}

static void test_conversions(void)
{
    /* Independently vary both readings, including both sides of shared nibble. */
    const uint32_t raw[] = {0u, 1u, 15u, 16u, 65535u, 65536u,
                            196608u, 262144u, 393216u, 524288u,
                            786432u, 1048574u, 1048575u};
    size_t h;
    size_t t;
    for (h = 0u; h < sizeof raw / sizeof raw[0]; ++h) {
        for (t = 0u; t < sizeof raw / sizeof raw[0]; ++t) {
            uint8_t frame[7];
            dht20_sample_t sample;
            make_frame(frame, 0x18u, raw[h], raw[t]);
            assert(dht20_protocol_parse_frame(frame, 7u, &sample) == APP_OK);
            assert_close(sample.relative_humidity_percent,
                         (double)raw[h] / 1048576.0 * 100.0);
            assert_close(sample.temperature_c,
                         (double)raw[t] / 1048576.0 * 200.0 - 50.0);
            assert(sample.valid);
        }
    }
}

static void test_walking_bits(void)
{
    /* Synthetic wire locations and decimal raw weights specified independently
     * of make_frame/parser shifts. Each row selects one bit in each reading.
     * These are project fixtures, not manufacturer-provided test vectors.
     */
    static const struct {
        uint8_t humidity_byte;
        uint8_t humidity_mask;
        uint8_t temperature_byte;
        uint8_t temperature_mask;
        uint32_t expected_raw;
    } cases[] = {
        {3u, 0x10u, 5u, 0x01u,      1u},
        {3u, 0x20u, 5u, 0x02u,      2u},
        {3u, 0x40u, 5u, 0x04u,      4u},
        {3u, 0x80u, 5u, 0x08u,      8u},
        {2u, 0x01u, 5u, 0x10u,     16u},
        {2u, 0x02u, 5u, 0x20u,     32u},
        {2u, 0x04u, 5u, 0x40u,     64u},
        {2u, 0x08u, 5u, 0x80u,    128u},
        {2u, 0x10u, 4u, 0x01u,    256u},
        {2u, 0x20u, 4u, 0x02u,    512u},
        {2u, 0x40u, 4u, 0x04u,   1024u},
        {2u, 0x80u, 4u, 0x08u,   2048u},
        {1u, 0x01u, 4u, 0x10u,   4096u},
        {1u, 0x02u, 4u, 0x20u,   8192u},
        {1u, 0x04u, 4u, 0x40u,  16384u},
        {1u, 0x08u, 4u, 0x80u,  32768u},
        {1u, 0x10u, 3u, 0x01u,  65536u},
        {1u, 0x20u, 3u, 0x02u, 131072u},
        {1u, 0x40u, 3u, 0x04u, 262144u},
        {1u, 0x80u, 3u, 0x08u, 524288u}
    };
    size_t i;
    for (i = 0u; i < sizeof cases / sizeof cases[0]; ++i) {
        uint8_t humidity_frame[7] = {0x18u, 0u, 0u, 0u, 0u, 0u, 0u};
        uint8_t temperature_frame[7] = {0x18u, 0u, 0u, 0u, 0u, 0u, 0u};
        dht20_sample_t sample;

        humidity_frame[cases[i].humidity_byte] = cases[i].humidity_mask;
        humidity_frame[6] = reference_crc(humidity_frame);
        assert(dht20_protocol_parse_frame(humidity_frame, 7u, &sample) == APP_OK);
        assert(sample.valid);
        assert_close(sample.relative_humidity_percent,
                     (double)cases[i].expected_raw * 100.0 / 1048576.0);
        assert(sample.temperature_c == -50.0f);

        temperature_frame[cases[i].temperature_byte] = cases[i].temperature_mask;
        temperature_frame[6] = reference_crc(temperature_frame);
        assert(dht20_protocol_parse_frame(temperature_frame, 7u, &sample) == APP_OK);
        assert(sample.valid);
        assert_close(sample.temperature_c,
                     (double)cases[i].expected_raw * 200.0 / 1048576.0 - 50.0);
        assert(sample.relative_humidity_percent == 0.0f);
    }
}

int main(void)
{
    test_valid_and_crc();
    puts("PASS: known frame/CRC and all 56 single-bit corruptions");
    test_status();
    puts("PASS: all 256 status bytes; failed outputs unchanged for valid=false/true");
    test_arguments();
    puts("PASS: null/invalid lengths; failed outputs unchanged for valid=false/true");
    test_conversions();
    puts("PASS: 169 humidity/temperature pairs including encoding boundaries");
    test_walking_bits();
    puts("PASS: 40 independent walking-bit frames (20 humidity + 20 temperature)");
    puts("All DHT20 protocol tests passed.");
    return 0;
}
