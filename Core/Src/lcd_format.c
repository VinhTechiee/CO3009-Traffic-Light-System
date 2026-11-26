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
        label = "Auto";
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

static void lcd_format_light(lcd_format_buffer_t *buffer, const char *label,
                             traffic_color_t color, uint32_t seconds)
{
    static const char color_letter[] = {
        [TRAFFIC_RED] = 'R', [TRAFFIC_YELLOW] = 'Y', [TRAFFIC_GREEN] = 'G'
    };
    lcd_format_text(buffer, label);
    lcd_format_put(buffer, color_letter[color]);
    if (seconds < 10U) {
        lcd_format_put(buffer, '0');
    }
    lcd_format_uint(buffer, seconds);
}

app_status_t lcd_format_countdown(char *output, size_t capacity, size_t width,
                                  const fsm_automatic_view_t *view)
{
    if (output == NULL || width == 0U || view == NULL
        || (unsigned)view->colors.road_1 > TRAFFIC_GREEN
        || (unsigned)view->colors.road_2 > TRAFFIC_GREEN) {
        return APP_INVALID_ARGUMENT;
    }
    lcd_format_buffer_t buffer = {output, lcd_format_limit(capacity, width), 0U};
    lcd_format_light(&buffer, "Auto A:", view->colors.road_1, view->road1_seconds);
    lcd_format_light(&buffer, " B:", view->colors.road_2, view->road2_seconds);
    return lcd_format_finish(&buffer);
}

app_status_t lcd_format_manual(char *output, size_t capacity, size_t width,
                               const traffic_state_t *phase)
{
    static const char *const colors[] = {
        [TRAFFIC_RED] = "RED", [TRAFFIC_YELLOW] = "YELLOW", [TRAFFIC_GREEN] = "GREEN"
    };
    if (output == NULL || width == 0U || phase == NULL
        || (unsigned)phase->road_1 > TRAFFIC_GREEN || (unsigned)phase->road_2 > TRAFFIC_GREEN) {
        return APP_INVALID_ARGUMENT;
    }
    lcd_format_buffer_t buffer = {output, lcd_format_limit(capacity, width), 0U};
    lcd_format_text(&buffer, "A:");
    lcd_format_text(&buffer, colors[phase->road_1]);
    lcd_format_text(&buffer, " B:");
    lcd_format_text(&buffer, colors[phase->road_2]);
    return lcd_format_finish(&buffer);
}

app_status_t lcd_format_timing(char *output, size_t capacity, size_t width,
                               const timing_config_t *config, traffic_color_t color)
{
    const char *label;
    uint32_t selected_ms;

    if (output == NULL || width == 0u || config == NULL
        || config->green_ms == 0u || config->yellow_ms == 0u
        || (uint64_t)config->green_ms + config->yellow_ms > config->red_ms) {
        return APP_INVALID_ARGUMENT;
    }

    switch (color) {
    case TRAFFIC_GREEN:
        label = " G:";
        selected_ms = config->green_ms;
        break;
    case TRAFFIC_YELLOW:
        label = " Y:";
        selected_ms = config->yellow_ms;
        break;
    default:
        return APP_INVALID_ARGUMENT;
    }
    const bool whole_seconds = config->red_ms % 1000U == 0U && selected_ms % 1000U == 0U;
    const uint32_t divisor = whole_seconds ? 1000U : 1U;
    lcd_format_buffer_t buffer = {output, lcd_format_limit(capacity, width), 0U};
    lcd_format_text(&buffer, "R:");
    lcd_format_uint(&buffer, config->red_ms / divisor);
    if (whole_seconds) {
        lcd_format_put(&buffer, 's');
    }
    lcd_format_text(&buffer, label);
    lcd_format_uint(&buffer, selected_ms / divisor);
    lcd_format_text(&buffer, whole_seconds ? "s" : "ms");
    return lcd_format_finish(&buffer);
}
