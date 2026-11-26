#include "lcd_display.h"

#include <stddef.h>
#include <string.h>

#include "fsm_manual.h"
#include "fsm_mode.h"
#include "lcd.h"
#include "lcd_format.h"
#include "lcd_presentation.h"

static struct {
    lcd_display_layout_t layout;
    uint8_t columns;
    bool configured;
} lcd_display;

app_status_t lcd_display_configure(const lcd_display_layout_t *layout)
{
    uint8_t rows;
    uint8_t columns;
    if (layout == NULL) {
        return APP_INVALID_ARGUMENT;
    }

    const app_status_t status = lcd_get_size(&rows, &columns);
    if (status != APP_OK) {
        return status;
    }
    if (layout->mode_row >= rows || layout->timing_row >= rows || layout->sensor_row >= rows
        || layout->mode_row == layout->timing_row || layout->mode_row == layout->sensor_row) {
        return APP_INVALID_ARGUMENT;
    }

    lcd_display.layout = *layout;
    lcd_display.columns = columns;
    lcd_display.configured = true;
    return APP_OK;
}

static void lcd_display_submit(uint8_t row, app_status_t status, const char *line)
{
    const char *fallback = lcd_display.columns >= 2U ? "--" : "-";
    (void)lcd_request_line(row, status == APP_OK ? line : fallback);
}

static app_status_t lcd_display_format_mode(char *line, size_t capacity, app_mode_t mode)
{
    fsm_automatic_view_t view;
    if (mode == APP_MODE_AUTOMATIC && fsm_automatic_get_view(&view) == APP_OK) {
        const app_status_t status = lcd_format_countdown(line, capacity, lcd_display.columns, &view);
        if (status != APP_NO_CAPACITY) {
            return status;
        }
    }
    return lcd_format_mode(line, capacity, lcd_display.columns, mode);
}

void lcd_display_task(void)
{
    if (!lcd_display.configured) {
        return;
    }

    char line[LCD_MAX_COLUMNS + 1U];
    app_mode_t mode;
    app_status_t status = fsm_mode_get(&mode);
    const bool manual = status == APP_OK && mode == APP_MODE_MANUAL;
    const bool tuning = status == APP_OK && mode == APP_MODE_TUNING;
    if (status == APP_OK) {
        status = lcd_display_format_mode(line, sizeof line, mode);
    }
    lcd_display_submit(lcd_display.layout.mode_row, status, line);

    if (manual) {
        traffic_state_t phase;
        status = fsm_manual_get_phase(&phase);
        if (status == APP_OK) {
            status = lcd_format_manual(line, sizeof line, lcd_display.columns, &phase);
        }
        lcd_display_submit(lcd_display.layout.timing_row, status, line);
    } else if (tuning) {
        fsm_tuning_view_t view;
        status = fsm_tuning_get(&view);
        if (status == APP_OK) {
            status = lcd_format_timing(line, sizeof line, lcd_display.columns,
                                       &view.draft, view.selected);
        }
        lcd_display_submit(lcd_display.layout.timing_row, status, line);
    } else if (lcd_display.layout.timing_row != lcd_display.layout.sensor_row) {
        (void)lcd_request_line(lcd_display.layout.timing_row, "");
    }

    if (!(manual || tuning) || lcd_display.layout.timing_row != lcd_display.layout.sensor_row) {
        line[0] = '\0';
        status = lcd_presentation_sensor_line(line, sizeof line, lcd_display.columns);
        if (status == APP_NO_CAPACITY && lcd_display.columns >= 5U
            && strncmp(line, "Stale", 5U) == 0) {
            (void)lcd_request_line(lcd_display.layout.sensor_row, "Stale");
        } else {
            lcd_display_submit(lcd_display.layout.sensor_row, status, line);
        }
    }
}
