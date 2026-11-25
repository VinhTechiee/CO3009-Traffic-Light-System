#include "lcd_format.h"

typedef struct {
    char *text;
    size_t capacity;
    size_t length;
} lcd_format_buffer_t;

static size_t lcd_format_limit(size_t capacity, size_t width)
{
    return width < capacity ? width + 1u : capacity;
}

static void lcd_format_put(lcd_format_buffer_t *buffer, char character)
{
    if (buffer->capacity != 0U && buffer->length < buffer->capacity - 1U) {
        buffer->text[buffer->length] = character;
    }
    ++buffer->length;
}

static void lcd_format_text(lcd_format_buffer_t *buffer, const char *text)
{
    while (*text != '\0') {
        lcd_format_put(buffer, *text++);
    }
}

static void lcd_format_uint(lcd_format_buffer_t *buffer, uint32_t value)
{
    char digits[10];
    size_t count = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (count != 0U) {
        lcd_format_put(buffer, digits[--count]);
    }
}

static app_status_t lcd_format_finish(lcd_format_buffer_t *buffer)
{
    const bool fits = buffer->length < buffer->capacity;
    if (buffer->capacity != 0U) {
        buffer->text[fits ? buffer->length : buffer->capacity - 1U] = '\0';
    }
    return fits ? APP_OK : APP_NO_CAPACITY;
}

static void lcd_format_measurement(lcd_format_buffer_t *buffer, float value, char unit)
{
    /* The caller checks the encoding range before scaling or casting. */
    const bool negative = value < 0.0f;
    const float magnitude = negative ? -value : value;
    const uint32_t tenths = (uint32_t)((double)magnitude * 10.0 + 0.5);
    if (negative && tenths != 0U) {
        lcd_format_put(buffer, '-');
    }
    lcd_format_uint(buffer, tenths / 10U);
    lcd_format_put(buffer, '.');
    lcd_format_put(buffer, (char)('0' + tenths % 10U));
    lcd_format_put(buffer, unit);
}

app_status_t lcd_format_sensor(char *output, size_t capacity, size_t width,
                               const dht20_sample_t *sample,
                               const dht20_info_t *info)
{
    if (output == NULL || width == 0u || info == NULL) {
        return APP_INVALID_ARGUMENT;
    }

    lcd_format_buffer_t buffer = {output, lcd_format_limit(capacity, width), 0U};
    switch (info->sample_status) {
    case DHT20_SAMPLE_NEVER:
        lcd_format_text(&buffer, "No data");
        return lcd_format_finish(&buffer);
    case DHT20_SAMPLE_UNAVAILABLE:
        lcd_format_text(&buffer, "Unavailable");
        return lcd_format_finish(&buffer);
    case DHT20_SAMPLE_FRESH:
        break;
    case DHT20_SAMPLE_STALE:
        lcd_format_text(&buffer, "Stale ");
        break;
    default:
        return APP_INVALID_ARGUMENT;
    }

    const bool valid = info->has_sample && sample != NULL && sample->valid;
    /* The encoding bounds also reject NaN and infinity. */
    lcd_format_text(&buffer, "T:");
    if (valid && sample->temperature_c >= -50.0f && sample->temperature_c < 150.0f) {
        lcd_format_measurement(&buffer, sample->temperature_c, 'C');
    } else {
        lcd_format_text(&buffer, "--");
    }
    lcd_format_text(&buffer, " H:");
    if (valid && sample->relative_humidity_percent >= 0.0f
        && sample->relative_humidity_percent < 100.0f) {
        lcd_format_measurement(&buffer, sample->relative_humidity_percent, '%');
    } else {
        lcd_format_text(&buffer, "--");
    }
    return lcd_format_finish(&buffer);
}

app_status_t lcd_format_mode(char *output, size_t capacity, size_t width,
                             app_mode_t mode)
{
    const char *label;

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
    lcd_format_buffer_t buffer = {output, lcd_format_limit(capacity, width), 0U};
    lcd_format_text(&buffer, label);
    return lcd_format_finish(&buffer);
}

app_status_t lcd_format_timing(char *output, size_t capacity, size_t width,
                               const timing_config_t *config, traffic_color_t color)
{
    const char *label;
    uint32_t duration_ms;

    if (output == NULL || width == 0u || config == NULL
        || config->green_ms == 0u || config->yellow_ms == 0u
        || (uint64_t)config->green_ms + config->yellow_ms > config->red_ms) {
        return APP_INVALID_ARGUMENT;
    }

    switch (color) {
    case TRAFFIC_RED:
        label = "Red";
        duration_ms = config->red_ms;
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
    lcd_format_buffer_t buffer = {output, lcd_format_limit(capacity, width), 0U};
    lcd_format_text(&buffer, label);
    lcd_format_put(&buffer, ' ');
    lcd_format_uint(&buffer, duration_ms);
    lcd_format_text(&buffer, " ms");
    return lcd_format_finish(&buffer);
}
