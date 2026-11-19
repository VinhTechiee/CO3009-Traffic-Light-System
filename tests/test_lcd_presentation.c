#include "lcd_presentation.h"

#include "dht20.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct {
    dht20_sample_t sample;
    dht20_info_t info;
    app_status_t sample_status;
    app_status_t info_status;
    bool copies_sample;
    unsigned calls;
} fake;

app_status_t dht20_get_sample(dht20_sample_t *sample)
{
    assert(fake.calls == 0u);
    ++fake.calls;
    if (fake.copies_sample) {
        *sample = fake.sample;
    }
    return fake.sample_status;
}

app_status_t dht20_get_info(dht20_info_t *info)
{
    assert(fake.calls == 1u);
    ++fake.calls;
    if (fake.info_status == APP_OK) {
        *info = fake.info;
    }
    return fake.info_status;
}

static void expect_line(dht20_sample_status_t state, app_status_t sample_status,
                        bool copies_sample, const char *expected)
{
    char line[32];

    fake.calls = 0u;
    fake.sample_status = sample_status;
    fake.copies_sample = copies_sample;
    fake.info.sample_status = state;
    fake.info.has_sample = copies_sample;
    assert(lcd_presentation_sensor_line(line, sizeof line, sizeof line) == APP_OK);
    assert(strcmp(line, expected) == 0);
    assert(fake.calls == 2u);
}

static void test_sensor_states(void)
{
    fake.sample = (dht20_sample_t){-3.25f, 56.25f, true};
    fake.info_status = APP_OK;
    expect_line(DHT20_SAMPLE_FRESH, APP_OK, true, "T:-3.3C H:56.3%");
    expect_line(DHT20_SAMPLE_STALE, APP_NOT_READY, true, "Stale T:-3.3C H:56.3%");
    expect_line(DHT20_SAMPLE_UNAVAILABLE, APP_IO_ERROR, true, "Unavailable");
    expect_line(DHT20_SAMPLE_UNAVAILABLE, APP_IO_ERROR, false, "Unavailable");
    expect_line(DHT20_SAMPLE_NEVER, APP_NOT_READY, false, "No data");

    /* The captured reading crosses the age threshold between the two calls. */
    expect_line(DHT20_SAMPLE_STALE, APP_OK, true, "Stale T:-3.3C H:56.3%");
    fake.sample.valid = false;
    expect_line(DHT20_SAMPLE_FRESH, APP_OK, true, "T:-- H:--");
}

static void test_getter_failures(void)
{
    char line[] = "guard";

    fake.calls = 0u;
    fake.copies_sample = false;
    fake.sample_status = APP_NOT_READY;
    fake.info_status = APP_NOT_READY; /* Before dht20_configure. */
    assert(lcd_presentation_sensor_line(line, sizeof line, 32u) == APP_NOT_READY);
    assert(fake.calls == 2u);
    assert(strcmp(line, "guard") == 0);

    fake.calls = 0u;
    fake.sample_status = APP_INVALID_ARGUMENT;
    assert(lcd_presentation_sensor_line(line, sizeof line, 32u) == APP_INVALID_ARGUMENT);
    assert(fake.calls == 1u);

    fake.calls = 0u;
    fake.sample_status = APP_OK;
    fake.info_status = APP_IO_ERROR;
    assert(lcd_presentation_sensor_line(line, sizeof line, 32u) == APP_IO_ERROR);
    assert(fake.calls == 2u);

    fake.calls = 0u;
    fake.sample_status = APP_IO_ERROR;
    fake.info_status = APP_OK;
    fake.info.sample_status = DHT20_SAMPLE_FRESH;
    assert(lcd_presentation_sensor_line(line, sizeof line, 32u) == APP_IO_ERROR);
    assert(fake.calls == 2u);

    fake.calls = 0u;
    fake.sample_status = APP_NOT_READY;
    assert(lcd_presentation_sensor_line(line, sizeof line, 32u) == APP_NOT_READY);
    assert(fake.calls == 2u);
    assert(strcmp(line, "guard") == 0);
}

static void test_buffer_and_arguments(void)
{
    char line[] = "!!!!!!!!!";

    fake.calls = 0u;
    assert(lcd_presentation_sensor_line(NULL, sizeof line, 32u) == APP_INVALID_ARGUMENT);
    assert(lcd_presentation_sensor_line(line, sizeof line, 0u) == APP_INVALID_ARGUMENT);
    assert(fake.calls == 0u);
    assert(strcmp(line, "!!!!!!!!!") == 0);

    fake.sample_status = APP_NOT_READY;
    fake.info.sample_status = DHT20_SAMPLE_NEVER;
    fake.info.has_sample = false;
    assert(lcd_presentation_sensor_line(line, 8u, 32u) == APP_OK);
    assert(memcmp(line, "No data\0!", sizeof line) == 0);
    memset(line, '!', sizeof line);
    fake.calls = 0u;
    assert(lcd_presentation_sensor_line(line, 7u, 32u) == APP_NO_CAPACITY);
    assert(memcmp(line, "No dat\0!!!", sizeof line) == 0);
    fake.calls = 0u;
    assert(lcd_presentation_sensor_line(line, sizeof line, 2u) == APP_NO_CAPACITY);
    assert(strcmp(line, "No") == 0);
    fake.calls = 0u;
    assert(lcd_presentation_sensor_line(line, 0u, 32u) == APP_NO_CAPACITY);
    assert(strcmp(line, "No") == 0);
}

int main(void)
{
    test_sensor_states();
    puts("PASS: sensor states, missing or invalid data, and snapshot freshness");
    test_getter_failures();
    puts("PASS: preconfiguration and getter failures");
    test_buffer_and_arguments();
    puts("PASS: caller width, buffer capacity, and invalid arguments");
    return 0;
}
