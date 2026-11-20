#include "lcd.h"

#include <string.h>

static struct {
    lcd_config_t config;
    char requested[LCD_MAX_ROWS][LCD_MAX_COLUMNS + 1U];
    char sending[LCD_MAX_COLUMNS + 1U];
    bool dirty[LCD_MAX_ROWS];
    uint8_t row;
    bool configured;
    bool initialized;
    bool prepared;
    bool writing;
} lcd;

app_status_t lcd_configure(const lcd_config_t *config)
{
    if (config == NULL || config->rows == 0U || config->rows > LCD_MAX_ROWS
        || config->columns == 0U || config->columns > LCD_MAX_COLUMNS
        || config->address_7bit < 0x08U || config->address_7bit > 0x77U
        || config->prepare == NULL || config->write_line == NULL
        || config->bus.transmit == NULL || config->bus.receive == NULL
        || config->bus.poll == NULL || config->bus.cancel == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (lcd.initialized) {
        return APP_BUSY;
    }

    lcd.config = *config;
    lcd.configured = true;
    return APP_OK;
}

app_status_t lcd_init(void)
{
    if (!lcd.configured) {
        return APP_NOT_READY;
    }
    if (lcd.initialized) {
        return APP_BUSY;
    }
    for (uint8_t row = 0U; row < lcd.config.rows; ++row) {
        memset(lcd.requested[row], ' ', lcd.config.columns);
        lcd.requested[row][lcd.config.columns] = '\0';
        lcd.dirty[row] = true;
    }
    lcd.initialized = true;
    return APP_OK;
}

app_status_t lcd_get_size(uint8_t *rows, uint8_t *columns)
{
    if (rows == NULL || columns == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (!lcd.initialized) {
        return APP_NOT_READY;
    }
    *rows = lcd.config.rows;
    *columns = lcd.config.columns;
    return APP_OK;
}

app_status_t lcd_request_line(uint8_t row, const char *text)
{
    char line[LCD_MAX_COLUMNS + 1U];
    size_t length = 0U;

    if (text == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (!lcd.initialized) {
        return APP_NOT_READY;
    }
    if (row >= lcd.config.rows) {
        return APP_INVALID_ARGUMENT;
    }
    while (length < lcd.config.columns && text[length] != '\0') {
        ++length;
    }
    if (text[length] != '\0') {
        return APP_NO_CAPACITY;
    }

    memcpy(line, text, length);
    memset(line + length, ' ', lcd.config.columns - length);
    line[lcd.config.columns] = '\0';
    if (strcmp(lcd.requested[row], line) != 0) {
        memcpy(lcd.requested[row], line, (size_t)lcd.config.columns + 1U);
        lcd.dirty[row] = true;
    }
    return APP_OK;
}

static bool lcd_start_line(void)
{
    for (uint8_t offset = 0U; offset < lcd.config.rows; ++offset) {
        const uint8_t row = (uint8_t)((lcd.row + offset) % lcd.config.rows);
        if (lcd.dirty[row]) {
            lcd.row = row;
            memcpy(lcd.sending, lcd.requested[row], (size_t)lcd.config.columns + 1U);
            lcd.dirty[row] = false;
            return true;
        }
    }
    return false;
}

void lcd_task(void)
{
    if (!lcd.initialized) {
        return;
    }
    if (!lcd.prepared) {
        lcd.prepared = lcd.config.prepare(lcd.config.context, &lcd.config.bus,
                                          lcd.config.address_7bit) == APP_OK;
        return;
    }
    if (!lcd.writing && !lcd_start_line()) {
        return;
    }

    const app_status_t status = lcd.config.write_line(lcd.config.context, &lcd.config.bus,
        lcd.config.address_7bit, lcd.row, lcd.sending, lcd.config.columns);
    lcd.writing = status == APP_BUSY || status == APP_NOT_READY;
    if (!lcd.writing) {
        if (status != APP_OK) {
            lcd.dirty[lcd.row] = true;
        }
        lcd.row = (uint8_t)((lcd.row + 1U) % lcd.config.rows);
    }
}
