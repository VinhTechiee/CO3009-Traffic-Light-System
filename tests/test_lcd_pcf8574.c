#include "lcd_pcf8574.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { COLUMNS = 16U, RS = 0x01U, E = 0x04U, BACKLIGHT = 0x08U, ADDRESS = 0x27U };

static uint32_t now_ms, completed_ms, cancel_completed_ms;
static unsigned calls, submissions, init_step, cancels, line_chunks, starts_with_cursor;
static bool hold, cancelling, nacked, contended;
static i2c_async_token_t current_token;
static const uint8_t *borrowed;
static const void *owner;
static size_t length;
static uint8_t snapshot[LCD_PCF8574_TRANSFER_BYTES];
static uint8_t cursor;
static char screen[2][COLUMNS];

static uint32_t clock_ms(void *context)
{
    assert(context == NULL);
    return now_ms;
}

static i2c_async_result_t transmit(void *context, const void *identity, uint8_t address,
    const uint8_t *data, size_t size, i2c_async_token_t *token)
{
    assert(context == NULL && ++calls == 1U && borrowed == NULL && address == ADDRESS);
    assert(identity != NULL && size > 0U && size <= sizeof snapshot && size % 3U == 0U);
    if (contended) {
        return I2C_ASYNC_BUSY;
    }
    if (init_step == 0U) {
        assert(now_ms >= 50U);
        if (cancel_completed_ms != 0U) {
            assert(now_ms - cancel_completed_ms >= 250U);
        }
        assert(size == 3U && data[0] == 0x38U && data[1] == 0x3CU && data[2] == 0x38U);
    }
    if (init_step == 1U) {
        assert(now_ms - completed_ms >= 5U);
    }
    if (init_step == 7U) {
        assert(now_ms - completed_ms >= 2U); /* Prior command cleared the LCD. */
    }
    ++submissions;
    borrowed = data;
    owner = identity;
    length = size;
    *token = ++current_token;
    memcpy(snapshot, data, size);
    return I2C_ASYNC_ACCEPTED;
}

static i2c_async_result_t receive(void *context, const void *identity, uint8_t address,
    uint8_t *data, size_t size, i2c_async_token_t *token)
{
    (void)context;
    (void)identity;
    (void)address;
    (void)data;
    (void)size;
    (void)token;
    assert(false); /* Write-only backpack. */
    return I2C_ASYNC_ERROR;
}

static uint8_t decode_nibble(size_t offset)
{
    const uint8_t pins = snapshot[offset];
    assert((pins & (E | BACKLIGHT | 0x40U)) == BACKLIGHT);
    assert(snapshot[offset + 1U] == (pins | E) && snapshot[offset + 2U] == pins);
    return (uint8_t)(((pins & 0x10U) >> 4U) | ((pins & 0x20U) >> 4U)
        | ((pins & 0x02U) << 1U) | ((pins & 0x80U) >> 4U));
}

static uint8_t decode_byte(size_t offset)
{
    assert((snapshot[offset] & RS) == (snapshot[offset + 3U] & RS));
    return (uint8_t)((decode_nibble(offset) << 4U) | decode_nibble(offset + 3U));
}

static void apply_packet(void)
{
    static const uint8_t startup[] = {0x03U, 0x03U, 0x03U, 0x02U, 0x28U,
                                       0x08U, 0x01U, 0x06U, 0x0CU};
    if (init_step < sizeof startup) {
        assert((snapshot[0] & RS) == 0U);
        assert(length == (init_step < 4U ? 3U : 6U));
        const uint8_t value = length == 3U ? decode_nibble(0U) : decode_byte(0U);
        assert(value == startup[init_step]);
        if (value == 0x01U) {
            memset(screen, ' ', sizeof screen);
        }
        ++init_step;
        return;
    }

    assert(length % 6U == 0U);
    ++line_chunks;
    for (size_t offset = 0U; offset < length; offset += 6U) {
        const uint8_t value = decode_byte(offset);
        if ((snapshot[offset] & RS) == 0U) {
            assert(offset == 0U && (value == 0x80U || value == 0xC0U));
            cursor = value & 0x7FU;
            ++starts_with_cursor;
        } else {
            assert((cursor & 0x3FU) < COLUMNS);
            screen[cursor >= 0x40U][cursor & 0x3FU] = (char)value;
            ++cursor;
        }
    }
}

static i2c_async_result_t poll(void *context, const void *identity, i2c_async_token_t token)
{
    assert(context == NULL && ++calls == 1U && identity == owner && token == current_token);
    assert(borrowed != NULL && memcmp(borrowed, snapshot, length) == 0);
    if (cancelling || nacked) {
        if (cancelling) {
            cancel_completed_ms = now_ms;
        }
        cancelling = false;
        nacked = false;
        borrowed = NULL;
        init_step = 0U;
        return I2C_ASYNC_ERROR;
    }
    if (hold) {
        return I2C_ASYNC_PENDING;
    }
    apply_packet();
    completed_ms = now_ms;
    borrowed = NULL;
    return I2C_ASYNC_COMPLETE;
}

static i2c_async_result_t cancel(void *context, const void *identity, i2c_async_token_t token)
{
    assert(context == NULL && ++calls == 1U && identity == owner && token == current_token);
    assert(borrowed != NULL && memcmp(borrowed, snapshot, length) == 0);
    ++cancels;
    cancelling = true;
    return I2C_ASYNC_PENDING;
}

static void run(unsigned milliseconds)
{
    for (unsigned step = 0U; step < milliseconds; ++step) {
        ++now_ms;
        calls = 0U;
        lcd_task();
    }
}

int main(void)
{
    const i2c_async_t bus = {NULL, transmit, receive, poll, cancel};
    lcd_pcf8574_profile_t profile = {.rows = 2U, .columns = COLUMNS, .address_7bit = ADDRESS,
        .rs_mask = RS, .enable_mask = E, .data_masks = {0x10U, 0x20U, 0x02U, 0x80U},
        .backlight_mask = BACKLIGHT, .backlight_active_high = true, .ddram_rows = {0U, 0x40U}};
    lcd_config_t lcd_config;
    lcd_pcf8574_t controller;
    profile.address_7bit = 0x38U;
    assert(lcd_pcf8574_configure(&controller, &bus, &profile, clock_ms, NULL, &lcd_config)
        == APP_INVALID_ARGUMENT);
    profile.address_7bit = ADDRESS;
    profile.ddram_rows[1] = 0x08U;
    assert(lcd_pcf8574_configure(&controller, &bus, &profile, clock_ms, NULL, &lcd_config)
        == APP_INVALID_ARGUMENT);
    profile.ddram_rows[1] = 0x40U;
    profile.data_masks[3] = E;
    assert(lcd_pcf8574_configure(&controller, &bus, &profile, clock_ms, NULL, &lcd_config)
        == APP_INVALID_ARGUMENT);
    profile.data_masks[3] = 0x80U;
    assert(lcd_pcf8574_configure(&controller, &bus, &profile, clock_ms, NULL, &lcd_config) == APP_OK);
    profile.data_masks[3] = E; /* Controller gets a copy. */
    assert(lcd_configure(&lcd_config) == APP_OK && lcd_init() == APP_OK);

    assert(lcd_request_line(0U, "Automatic") == APP_OK);
    assert(lcd_request_line(1U, "T=25 C RH=50 %") == APP_OK);
    run(49U);
    assert(submissions == 0U);
    contended = true;
    run(5U);
    assert(submissions == 0U);
    contended = false;
    run(60U);
    assert(init_step == 9U && line_chunks == 6U && starts_with_cursor == 2U);
    assert(memcmp(screen[0], "Automatic       ", COLUMNS) == 0);
    assert(memcmp(screen[1], "T=25 C RH=50 %  ", COLUMNS) == 0);

    assert(lcd_request_line(0U, "Manual") == APP_OK);
    run(1U);
    assert(borrowed != NULL);
    nacked = true;
    run(1U);
    const unsigned after_error = submissions;
    run(249U);
    assert(submissions == after_error);
    run(60U);
    assert(init_step == 9U && starts_with_cursor == 4U);
    assert(memcmp(screen[0], "Manual          ", COLUMNS) == 0);
    assert(memcmp(screen[1], "T=25 C RH=50 %  ", COLUMNS) == 0);

    assert(lcd_request_line(0U, "Tuning") == APP_OK);
    run(1U);
    assert(borrowed != NULL);
    hold = true;
    run(99U);
    assert(cancels == 0U);
    assert(lcd_request_line(0U, "Automatic") == APP_OK); /* Update during borrow. */
    run(1U);
    assert(cancels == 1U && borrowed != NULL);
    run(1U);
    assert(borrowed == NULL && cancel_completed_ms != 0U);
    hold = false;
    run(310U);
    assert(init_step == 9U && starts_with_cursor == 6U);
    assert(memcmp(screen[0], "Automatic       ", COLUMNS) == 0);
    assert(memcmp(screen[1], "T=25 C RH=50 %  ", COLUMNS) == 0);
    puts("PCF8574 LCD tests passed: initialization, wire bytes, contention and recovery repaint");
    return 0;
}
