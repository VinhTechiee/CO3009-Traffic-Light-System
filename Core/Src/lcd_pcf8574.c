#include "lcd_pcf8574.h"

enum {
    LCD_PCF8574_STARTUP_MS = 50U,
    LCD_PCF8574_TRANSFER_TIMEOUT_MS = 100U,
    LCD_PCF8574_RETRY_MS = 250U
};

static bool lcd_pcf8574_mask_valid(uint8_t mask, uint8_t *used)
{
    if (mask == 0U || (mask & (mask - 1U)) != 0U || (*used & mask) != 0U) {
        return false;
    }
    *used |= mask;
    return true;
}

static bool lcd_pcf8574_profile_valid(const lcd_pcf8574_profile_t *profile)
{
    if (profile == NULL || profile->rows == 0U || profile->rows > LCD_MAX_ROWS
        || profile->columns == 0U || profile->columns > LCD_MAX_COLUMNS
        || !((profile->address_7bit >= 0x20U && profile->address_7bit <= 0x27U)
            || (profile->address_7bit >= 0x39U && profile->address_7bit <= 0x3FU))) {
        /* 0x38 is reserved for DHT20 on the shared bus. */
        return false;
    }

    uint8_t used = 0U;
    if (!lcd_pcf8574_mask_valid(profile->rs_mask, &used)
        || !lcd_pcf8574_mask_valid(profile->enable_mask, &used)) {
        return false;
    }
    for (size_t bit = 0U; bit < 4U; ++bit) {
        if (!lcd_pcf8574_mask_valid(profile->data_masks[bit], &used)) {
            return false;
        }
    }
    if (profile->backlight_mask != 0U
        && !lcd_pcf8574_mask_valid(profile->backlight_mask, &used)) {
        return false;
    }

    for (size_t row = 0U; row < profile->rows; ++row) {
        const unsigned start = profile->ddram_rows[row];
        const unsigned end = start + profile->columns;
        const bool usable = profile->rows == 1U ? end <= 0x50U
            : (end <= 0x28U || (start >= 0x40U && end <= 0x68U));
        if (!usable) {
            return false;
        }
        for (size_t previous = 0U; previous < row; ++previous) {
            const unsigned other = profile->ddram_rows[previous];
            if (start < other + profile->columns && other < end) {
                return false;
            }
        }
    }
    return true;
}

static void lcd_pcf8574_append_nibble(lcd_pcf8574_t *controller, uint8_t nibble, bool data)
{
    const lcd_pcf8574_profile_t *const profile = &controller->profile;
    uint8_t value = profile->backlight_active_high ? profile->backlight_mask : 0U;
    if (data) {
        value |= profile->rs_mask;
    }
    for (size_t bit = 0U; bit < 4U; ++bit) {
        if ((nibble & (1U << bit)) != 0U) {
            value |= profile->data_masks[bit];
        }
    }
    /* Each expander write has a full I2C byte time between pin changes.
     * Data settles before E goes high and is held on E's falling edge. */
    controller->tx[controller->tx_length++] = value;
    controller->tx[controller->tx_length++] = value | profile->enable_mask;
    controller->tx[controller->tx_length++] = value;
}

static void lcd_pcf8574_append_byte(lcd_pcf8574_t *controller, uint8_t byte, bool data)
{
    lcd_pcf8574_append_nibble(controller, byte >> 4U, data);
    lcd_pcf8574_append_nibble(controller, byte & 0x0FU, data);
}

static app_status_t lcd_pcf8574_end_transfer(lcd_pcf8574_t *controller, app_status_t status)
{
    controller->tx_length = 0U;
    controller->accepted = false;
    controller->cancelling = false;
    return status;
}

/* One bus operation per call. Do not overwrite the packet until terminal. */
static app_status_t lcd_pcf8574_service(lcd_pcf8574_t *controller, const i2c_async_t *bus,
                                      uint8_t address, uint32_t now)
{
    const bool timed_out = now - controller->transfer_started_ms >= LCD_PCF8574_TRANSFER_TIMEOUT_MS;
    i2c_async_result_t result;
    if (!controller->accepted) {
        if (timed_out) {
            return lcd_pcf8574_end_transfer(controller, APP_IO_ERROR);
        }
        result = bus->transmit(bus->context, controller, address, controller->tx,
                               controller->tx_length, &controller->token);
        if (result == I2C_ASYNC_BUSY) {
            return APP_BUSY;
        }
        if (result != I2C_ASYNC_ACCEPTED) {
            return lcd_pcf8574_end_transfer(controller, APP_IO_ERROR);
        }
        controller->accepted = true;
        controller->transfer_started_ms = controller->clock_ms(controller->clock_context);
        return APP_NOT_READY;
    }

    if (timed_out && !controller->cancelling) {
        controller->cancelling = true;
        result = bus->cancel(bus->context, controller, controller->token);
    } else {
        result = bus->poll(bus->context, controller, controller->token);
    }
    if (result != I2C_ASYNC_COMPLETE && result != I2C_ASYNC_ERROR) {
        return APP_NOT_READY;
    }
    const bool complete = result == I2C_ASYNC_COMPLETE && !controller->cancelling;
    return lcd_pcf8574_end_transfer(controller, complete ? APP_OK : APP_IO_ERROR);
}

static void lcd_pcf8574_retry(lcd_pcf8574_t *controller)
{
    controller->prepared = false;
    controller->init_step = 0U;
    controller->column = 0U;
    controller->wait_started_ms = controller->clock_ms(controller->clock_context);
    controller->wait_ms = LCD_PCF8574_RETRY_MS;
}

static app_status_t lcd_pcf8574_prepare(void *context, const i2c_async_t *bus, uint8_t address)
{
    static const uint8_t commands[] = {0x03U, 0x03U, 0x03U, 0x02U, 0x20U,
                                       0x08U, 0x01U, 0x06U, 0x0CU};
    static const uint8_t wait_ms[] = {5U, 1U, 1U, 1U, 1U, 1U, 2U, 1U, 1U};
    lcd_pcf8574_t *const controller = context;
    const uint32_t now = controller->clock_ms(controller->clock_context);
    if (now - controller->wait_started_ms < controller->wait_ms) {
        return APP_NOT_READY;
    }
    const uint8_t step = controller->init_step;
    if (step == sizeof commands) {
        controller->prepared = true;
        return APP_OK;
    }

    if (controller->tx_length == 0U) {
        uint8_t command = commands[step];
        if (step < 4U) {
            lcd_pcf8574_append_nibble(controller, command, false);
        } else {
            if (step == 4U && controller->profile.rows > 1U) {
                command |= 0x08U;
            }
            lcd_pcf8574_append_byte(controller, command, false);
        }
        controller->transfer_started_ms = now;
    }
    const app_status_t status = lcd_pcf8574_service(controller, bus, address, now);
    if (status == APP_IO_ERROR) {
        lcd_pcf8574_retry(controller);
    } else if (status == APP_OK) {
        ++controller->init_step;
        controller->wait_ms = wait_ms[step];
        controller->wait_started_ms = controller->clock_ms(controller->clock_context);
        return APP_NOT_READY;
    }
    return status;
}

static app_status_t lcd_pcf8574_write_line(void *context, const i2c_async_t *bus,
    uint8_t address, uint8_t row, const char *text, uint8_t length)
{
    lcd_pcf8574_t *const controller = context;
    if (!controller->prepared || row >= controller->profile.rows || text == NULL
        || length != controller->profile.columns) {
        return APP_INVALID_ARGUMENT;
    }
    const uint32_t now = controller->clock_ms(controller->clock_context);
    if (controller->tx_length == 0U) {
        uint8_t column = controller->column;
        if (column == 0U) {
            const uint8_t set_address = 0x80U | controller->profile.ddram_rows[row];
            lcd_pcf8574_append_byte(controller, set_address, false);
        }
        while (column < length && controller->tx_length + 6U <= sizeof controller->tx) {
            lcd_pcf8574_append_byte(controller, (uint8_t)text[column], true);
            ++column;
        }
        controller->next_column = column;
        controller->transfer_started_ms = now;
    }

    const app_status_t status = lcd_pcf8574_service(controller, bus, address, now);
    if (status == APP_IO_ERROR) {
        lcd_pcf8574_retry(controller);
    } else if (status == APP_OK) {
        controller->column = controller->next_column;
        if (controller->column < length) {
            return APP_NOT_READY;
        }
        controller->column = 0U;
    }
    return status;
}

app_status_t lcd_pcf8574_configure(lcd_pcf8574_t *controller, const i2c_async_t *bus,
    const lcd_pcf8574_profile_t *profile, uint32_t (*clock_ms)(void *),
    void *clock_context, lcd_config_t *lcd_config)
{
    if (controller == NULL || bus == NULL || clock_ms == NULL || lcd_config == NULL
        || bus->transmit == NULL || bus->receive == NULL || bus->poll == NULL
        || bus->cancel == NULL || !lcd_pcf8574_profile_valid(profile)) {
        return APP_INVALID_ARGUMENT;
    }
    *controller = (lcd_pcf8574_t){.profile = *profile, .clock_ms = clock_ms,
        .clock_context = clock_context, .wait_started_ms = clock_ms(clock_context),
        .wait_ms = LCD_PCF8574_STARTUP_MS};
    *lcd_config = (lcd_config_t){profile->rows, profile->columns, profile->address_7bit,
        *bus, controller, lcd_pcf8574_prepare, lcd_pcf8574_write_line};
    return APP_OK;
}
