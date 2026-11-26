#include "lcd_format.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_sensor(void)
{
    char line[32];
    dht20_sample_t sample = {23.5f, 56.25f, true};
    dht20_info_t info = {.sample_status = DHT20_SAMPLE_FRESH, .has_sample = true};

    assert(lcd_format_sensor(line, sizeof line, 16u, &sample, &info) == APP_OK);
    assert(strcmp(line, "T:23.5C H:56.3%") == 0);
    sample.temperature_c = -3.25f;
    assert(lcd_format_sensor(line, sizeof line, 16u, &sample, &info) == APP_OK);
    assert(strcmp(line, "T:-3.3C H:56.3%") == 0);
    sample.temperature_c = -0.01f;
    sample.relative_humidity_percent = 99.99f;
    assert(lcd_format_sensor(line, sizeof line, 16u, &sample, &info) == APP_OK);
    assert(strcmp(line, "T:0.0C H:100.0%") == 0);

    info.sample_status = DHT20_SAMPLE_STALE;
    assert(lcd_format_sensor(line, sizeof line, 24u, &sample, &info) == APP_OK);
    assert(strcmp(line, "Stale T:0.0C H:100.0%") == 0);
    assert(lcd_format_sensor(line, sizeof line, 5u, &sample, &info) == APP_NO_CAPACITY);
    assert(strcmp(line, "Stale") == 0);
    info.sample_status = DHT20_SAMPLE_UNAVAILABLE;
    assert(lcd_format_sensor(line, sizeof line, 16u, &sample, &info) == APP_OK);
    assert(strcmp(line, "Unavailable") == 0);
    info.sample_status = DHT20_SAMPLE_NEVER;
    info.has_sample = false;
    assert(lcd_format_sensor(line, sizeof line, 16u, NULL, &info) == APP_OK);
    assert(strcmp(line, "No data") == 0);

    info.sample_status = DHT20_SAMPLE_FRESH;
    assert(lcd_format_sensor(line, sizeof line, 16u, &sample, &info) == APP_OK);
    assert(strcmp(line, "T:-- H:--") == 0);
    info.has_sample = true;
    assert(lcd_format_sensor(line, sizeof line, 16u, NULL, &info) == APP_OK);
    assert(strcmp(line, "T:-- H:--") == 0);
    sample.valid = false;
    assert(lcd_format_sensor(line, sizeof line, 16u, &sample, &info) == APP_OK);
    assert(strcmp(line, "T:-- H:--") == 0);
    sample.valid = true;
    sample.temperature_c = NAN;
    assert(lcd_format_sensor(line, sizeof line, 16u, &sample, &info) == APP_OK);
    assert(strcmp(line, "T:-- H:100.0%") == 0);
    sample.temperature_c = -50.0f;
    sample.relative_humidity_percent = INFINITY;
    assert(lcd_format_sensor(line, sizeof line, 16u, &sample, &info) == APP_OK);
    assert(strcmp(line, "T:-50.0C H:--") == 0);
    sample.temperature_c = 150.0f;
    sample.relative_humidity_percent = -0.5f;
    assert(lcd_format_sensor(line, sizeof line, 16u, &sample, &info) == APP_OK);
    assert(strcmp(line, "T:-- H:--") == 0);
}

static void test_sensor_buffer_bounds(void)
{
    static const char expected[] = "T:-3.3C H:56.3%";
    const dht20_sample_t sample = {-3.25f, 56.25f, true};
    const dht20_info_t info = {.sample_status = DHT20_SAMPLE_FRESH, .has_sample = true};
    char line[sizeof expected + 2u];

    memset(line, '!', sizeof line);
    assert(lcd_format_sensor(line, sizeof expected, 32u, &sample, &info) == APP_OK);
    assert(memcmp(line, expected, sizeof expected) == 0);
    assert(line[sizeof expected] == '!' && line[sizeof expected + 1u] == '!');

    memset(line, '!', sizeof line);
    assert(lcd_format_sensor(line, sizeof expected - 1u, 32u, &sample, &info)
           == APP_NO_CAPACITY);
    assert(memcmp(line, expected, sizeof expected - 2u) == 0);
    assert(line[sizeof expected - 2u] == '\0');
    assert(memcmp(line + sizeof expected - 1u, "!!!", 3u) == 0);

    memset(line, '!', sizeof line);
    assert(lcd_format_sensor(line, sizeof line, 0u, &sample, &info)
           == APP_INVALID_ARGUMENT);
    for (size_t i = 0u; i < sizeof line; ++i) {
        assert(line[i] == '!');
    }
    assert(lcd_format_sensor(NULL, sizeof line, 32u, &sample, &info)
           == APP_INVALID_ARGUMENT);
}

static void test_mode_and_bounds(void)
{
    char line[16] = "sentinel";
    char small[] = "guard";

    assert(lcd_format_mode(line, sizeof line, 4u, APP_MODE_AUTOMATIC) == APP_OK);
    assert(strcmp(line, "Auto") == 0);
    assert(lcd_format_mode(line, sizeof line, 3u, APP_MODE_AUTOMATIC) == APP_NO_CAPACITY);
    assert(strcmp(line, "Aut") == 0);
    assert(lcd_format_mode(line, sizeof line, SIZE_MAX, APP_MODE_MANUAL) == APP_OK);
    assert(strcmp(line, "Manual") == 0);
    assert(lcd_format_mode(line, sizeof line, 6u, APP_MODE_TUNING) == APP_OK);
    assert(strcmp(line, "Tuning") == 0);

    assert(lcd_format_mode(small, 3u, 20u, APP_MODE_MANUAL) == APP_NO_CAPACITY);
    assert(memcmp(small, "Ma\0rd", sizeof small) == 0);
    assert(lcd_format_mode(small, 1u, 20u, APP_MODE_TUNING) == APP_NO_CAPACITY);
    assert(memcmp(small, "\0a\0rd", sizeof small) == 0);
    assert(lcd_format_mode(line, 0u, 20u, APP_MODE_MANUAL) == APP_NO_CAPACITY);
    assert(strcmp(line, "Tuning") == 0);
}

static void test_countdown(void)
{
    char line[17];
    fsm_automatic_view_t view = {{TRAFFIC_RED, TRAFFIC_GREEN}, 5U, 3U};
    assert(lcd_format_countdown(line, sizeof line, 16U, &view) == APP_OK);
    assert(strcmp(line, "Auto A:R05 B:G03") == 0);
    view = (fsm_automatic_view_t){{TRAFFIC_YELLOW, TRAFFIC_RED}, 0U, 40U};
    assert(lcd_format_countdown(line, sizeof line, 16U, &view) == APP_OK);
    assert(strcmp(line, "Auto A:Y00 B:R40") == 0);
    assert(lcd_format_countdown(line, sizeof line, 15U, &view) == APP_NO_CAPACITY);
    assert(strcmp(line, "Auto A:Y00 B:R4") == 0);
}

static void test_manual_colors(void)
{
    char line[17];
    traffic_state_t phase = {TRAFFIC_YELLOW, TRAFFIC_RED};
    assert(lcd_format_manual(line, sizeof line, 16U, &phase) == APP_OK);
    assert(strcmp(line, "A:YELLOW B:RED") == 0);
    phase = (traffic_state_t){TRAFFIC_RED, TRAFFIC_GREEN};
    assert(lcd_format_manual(line, sizeof line, 16U, &phase) == APP_OK);
    assert(strcmp(line, "A:RED B:GREEN") == 0);
    assert(lcd_format_manual(line, sizeof line, 12U, &phase) == APP_NO_CAPACITY);
    assert(strcmp(line, "A:RED B:GREE") == 0);
}

static void test_timing_and_invalid_arguments(void)
{
    char line[32];
    timing_config_t config = {.red_ms = 8000u, .green_ms = 5000u, .yellow_ms = 2750u};
    const dht20_info_t invalid_info = {.sample_status = (dht20_sample_status_t)99};

    assert(lcd_format_timing(line, sizeof line, 16u, &config, TRAFFIC_GREEN) == APP_OK);
    assert(strcmp(line, "R:8s G:5s") == 0);
    assert(lcd_format_timing(line, sizeof line, 16u, &config, TRAFFIC_YELLOW) == APP_OK);
    assert(strcmp(line, "R:8000 Y:2750ms") == 0);
    assert(lcd_format_timing(line, sizeof line, 16u, &config, TRAFFIC_RED)
           == APP_INVALID_ARGUMENT);
    assert(config.red_ms == 8000u);
    config.red_ms = UINT32_MAX;
    config.green_ms = UINT32_MAX - config.yellow_ms;
    assert(lcd_format_timing(line, sizeof line, 32u, &config, TRAFFIC_GREEN) == APP_OK);
    assert(strcmp(line, "R:4294967295 G:4294964545ms") == 0);
    ++config.green_ms;
    assert(lcd_format_timing(line, sizeof line, 32u, &config, TRAFFIC_GREEN)
           == APP_INVALID_ARGUMENT);
    config.green_ms = 0u;
    assert(lcd_format_timing(line, sizeof line, 20u, &config, TRAFFIC_YELLOW)
           == APP_INVALID_ARGUMENT);
    config.green_ms = 5000u;
    assert(lcd_format_timing(line, sizeof line, 16u, &config, (traffic_color_t)99)
           == APP_INVALID_ARGUMENT);
    assert(lcd_format_timing(line, sizeof line, 16u, NULL, TRAFFIC_GREEN)
           == APP_INVALID_ARGUMENT);
    assert(lcd_format_mode(line, sizeof line, 16u, (app_mode_t)99)
           == APP_INVALID_ARGUMENT);
    assert(lcd_format_mode(line, sizeof line, 0u, APP_MODE_MANUAL)
           == APP_INVALID_ARGUMENT);
    assert(lcd_format_mode(NULL, 0u, 16u, APP_MODE_MANUAL) == APP_INVALID_ARGUMENT);
    assert(lcd_format_sensor(line, sizeof line, 16u, NULL, NULL) == APP_INVALID_ARGUMENT);
    assert(lcd_format_sensor(line, sizeof line, 16u, NULL, &invalid_info)
           == APP_INVALID_ARGUMENT);
    assert(strcmp(line, "R:4294967295 G:4294964545ms") == 0);
}

static void test_timing_buffer_bounds(void)
{
    static const char expected[] = "R:7750 Y:2750ms";
    const timing_config_t config = {.red_ms = 7750u, .green_ms = 5000u, .yellow_ms = 2750u};
    char line[sizeof expected + 2u];

    memset(line, '!', sizeof line);
    assert(lcd_format_timing(line, sizeof expected, 32u, &config, TRAFFIC_YELLOW)
           == APP_OK);
    assert(memcmp(line, expected, sizeof expected) == 0);
    assert(line[sizeof expected] == '!' && line[sizeof expected + 1u] == '!');

    memset(line, '!', sizeof line);
    assert(lcd_format_timing(line, sizeof expected - 1u, 32u, &config, TRAFFIC_YELLOW)
           == APP_NO_CAPACITY);
    assert(memcmp(line, expected, sizeof expected - 2u) == 0);
    assert(line[sizeof expected - 2u] == '\0');
    assert(memcmp(line + sizeof expected - 1u, "!!!", 3u) == 0);

    memset(line, '!', sizeof line);
    assert(lcd_format_timing(line, sizeof line, 0u, &config, TRAFFIC_YELLOW)
           == APP_INVALID_ARGUMENT);
    for (size_t i = 0u; i < sizeof line; ++i) {
        assert(line[i] == '!');
    }
    assert(lcd_format_timing(NULL, sizeof line, 32u, &config, TRAFFIC_YELLOW)
           == APP_INVALID_ARGUMENT);
}

int main(void)
{
    test_sensor();
    puts("PASS: sensor values, freshness, and missing or invalid readings");
    test_mode_and_bounds();
    puts("PASS: mode labels, widths, and bounded truncation");
    test_countdown();
    puts("PASS: countdown format and widths");
    test_manual_colors();
    puts("PASS: manual colors and widths");
    test_timing_and_invalid_arguments();
    puts("PASS: paired timing, overflow, and invalid arguments");
    test_sensor_buffer_bounds();
    test_timing_buffer_bounds();
    puts("PASS: sensor and timing exact-fit buffers, truncation, and guard bytes");
    return 0;
}
