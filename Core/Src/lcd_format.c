#include "lcd_format.h"

#include <inttypes.h>
#include <stdio.h>

static size_t lcd_format_limit(size_t capacity, size_t width)
{
    return width < capacity ? width + 1u : capacity;
}

static app_status_t lcd_format_result(int length, size_t limit)
{
    if (length < 0) {
        return APP_IO_ERROR;
    }
    return (size_t)length < limit ? APP_OK : APP_NO_CAPACITY;
}

static void lcd_format_measurement(char output[8], float value, char unit)
{
    /* The caller checks the encoding range before scaling or casting.
     * Format integer tenths to avoid requiring embedded printf float support.
     */
    const bool negative = value < 0.0f;
    const float magnitude = negative ? -value : value;
    const unsigned tenths = (unsigned)((double)magnitude * 10.0 + 0.5);
    if (tenths > 1500u) {
        return;
    }
    (void)snprintf(output, 8u, "%s%u.%u%c",
                   negative && tenths != 0u ? "-" : "",
                   tenths / 10u, tenths % 10u, unit);
}

app_status_t lcd_format_sensor(char *output, size_t capacity, size_t width,
                               const dht20_sample_t *sample,
                               const dht20_info_t *info)
{
    char temperature[8] = "--";
    char humidity[8] = "--";
    const char *prefix;
    size_t limit;

    if (output == NULL || width == 0u || info == NULL) {
        return APP_INVALID_ARGUMENT;
    }

    limit = lcd_format_limit(capacity, width);
    switch (info->sample_status) {
    case DHT20_SAMPLE_NEVER:
        return lcd_format_result(snprintf(output, limit, "No data"), limit);
    case DHT20_SAMPLE_UNAVAILABLE:
        return lcd_format_result(snprintf(output, limit, "Unavailable"), limit);
    case DHT20_SAMPLE_FRESH:
        prefix = "";
        break;
    case DHT20_SAMPLE_STALE:
        prefix = "Stale ";
        break;
    default:
        return APP_INVALID_ARGUMENT;
    }

    if (info->has_sample && sample != NULL && sample->valid) {
        /* Protocol encoding bounds, including rejection of NaN/infinity. */
        if (sample->temperature_c >= -50.0f && sample->temperature_c < 150.0f) {
            lcd_format_measurement(temperature, sample->temperature_c, 'C');
        }
        if (sample->relative_humidity_percent >= 0.0f
            && sample->relative_humidity_percent < 100.0f) {
            lcd_format_measurement(humidity, sample->relative_humidity_percent, '%');
        }
    }
    return lcd_format_result(
        snprintf(output, limit, "%sT:%s H:%s", prefix, temperature, humidity), limit);
}

app_status_t lcd_format_mode(char *output, size_t capacity, size_t width,
                             app_mode_t mode)
{
    const char *label;
    const size_t limit = lcd_format_limit(capacity, width);

    if (output == NULL || width == 0u) {
        return APP_INVALID_ARGUMENT;
    }

    switch (mode) {
    case APP_MODE_AUTOMATIC:
        label = "Automatic";
        break;
    case APP_MODE_MANUAL:
        label = "Manual";
        break;
    case APP_MODE_TUNING:
        label = "Tuning";
        break;
    default:
        return APP_INVALID_ARGUMENT;
    }
    return lcd_format_result(snprintf(output, limit, "%s", label), limit);
}

app_status_t lcd_format_timing(char *output, size_t capacity, size_t width,
                               const timing_config_t *config, traffic_color_t color)
{
    const char *label;
    uint32_t duration_ms;
    const size_t limit = lcd_format_limit(capacity, width);

    if (output == NULL || width == 0u || config == NULL
        || config->green_ms == 0u || config->yellow_ms == 0u
        || config->green_ms > UINT32_MAX - config->yellow_ms) {
        return APP_INVALID_ARGUMENT;
    }

    switch (color) {
    case TRAFFIC_RED:
        label = "Red";
        duration_ms = config->green_ms + config->yellow_ms;
        break;
    case TRAFFIC_GREEN:
        label = "Green";
        duration_ms = config->green_ms;
        break;
    case TRAFFIC_YELLOW:
        label = "Yellow";
        duration_ms = config->yellow_ms;
        break;
    default:
        return APP_INVALID_ARGUMENT;
    }
    return lcd_format_result(
        snprintf(output, limit, "%s %" PRIu32 " ms", label, duration_ms), limit);
}
