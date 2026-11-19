#include "lcd_presentation.h"

#include "dht20.h"
#include "lcd_format.h"

app_status_t lcd_presentation_sensor_line(char *output, size_t capacity, size_t width)
{
    dht20_sample_t sample = {0};
    dht20_info_t info;
    app_status_t sample_status;
    app_status_t info_status;

    if (output == NULL || width == 0u) {
        return APP_INVALID_ARGUMENT;
    }

    sample_status = dht20_get_sample(&sample);
    if (sample_status != APP_OK && sample_status != APP_NOT_READY
        && sample_status != APP_IO_ERROR) {
        return sample_status;
    }

    /* Only foreground driver work can replace the stored sample. Read metadata
     * immediately after copying it so aging during the two calls is reflected.
     */
    info_status = dht20_get_info(&info);
    if (info_status != APP_OK) {
        return info_status;
    }
    if ((sample_status == APP_IO_ERROR && info.sample_status != DHT20_SAMPLE_UNAVAILABLE)
        || (sample_status == APP_NOT_READY && info.sample_status == DHT20_SAMPLE_FRESH)) {
        return sample_status;
    }

    return lcd_format_sensor(output, capacity, width, &sample, &info);
}
