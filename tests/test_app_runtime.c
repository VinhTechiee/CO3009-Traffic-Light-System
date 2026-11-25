#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "app_runtime.h"
#include "button.h"
#include "dht20_protocol.h"
#include "fsm_automatic.h"
#include "fsm_mode.h"
#include "scheduler.h"
#include "traffic_light.h"

enum { LCD_ADDRESS = 0x27U, LCD_COLUMNS = 20U, DHT_ADDRESS = 0x38U };
enum { ROAD1_PINS = (1U << 3U) | (1U << 5U), ROAD2_PINS = (1U << 4U) | (1U << 10U) };
static uint32_t now_ms;
static uint8_t pressed;
static uint16_t gpio_b;
static unsigned gpio_transitions;
static bool sensor_missing;
static bool display_missing;
static unsigned sensor_frames;
static unsigned bus_contentions;
static char screen[3][LCD_COLUMNS + 1];
static const int lcd_owner = 0;

static struct {
    const void *owner;
    i2c_async_token_t token;
    const uint8_t *tx;
    uint8_t *rx;
    size_t length;
    uint32_t ready_ms;
    uint8_t address;
    uint8_t saved_tx[LCD_COLUMNS];
} transfer;

static uint32_t clock_ms(void *context)
{
    (void)context;
    return now_ms;
}

static uint32_t button_clock_ms(void)
{
    return now_ms;
}

static bool button_level(button_id_t id)
{
    return (pressed & BUTTON_MASK(id)) == 0U;
}

static void check_gpio_safety(void)
{
    assert((gpio_b & ROAD1_PINS) != ROAD1_PINS || (gpio_b & ROAD2_PINS) != ROAD2_PINS);
}

static void write_pins(uint16_t pins, bool high)
{
    const uint16_t previous = gpio_b;
    gpio_b = high ? (uint16_t)(gpio_b | pins) : (uint16_t)(gpio_b & (uint16_t)~pins);
    if (gpio_b != previous) {
        ++gpio_transitions;
    }
    check_gpio_safety();
}

static i2c_async_result_t start(const void *owner, uint8_t address, size_t length,
                               i2c_async_token_t *token)
{
    if (transfer.owner != NULL) {
        assert(transfer.owner != owner);
        ++bus_contentions;
        return I2C_ASYNC_BUSY;
    }
    transfer.owner = owner;
    transfer.address = address;
    transfer.length = length;
    transfer.ready_ms = now_ms + 20U;
    *token = ++transfer.token;
    return I2C_ASYNC_ACCEPTED;
}

static i2c_async_result_t tx(void *context, const void *owner, uint8_t address,
                            const uint8_t *bytes, size_t length, i2c_async_token_t *token)
{
    (void)context;
    if (address == DHT_ADDRESS) {
        const uint8_t trigger[] = {0xACU, 0x33U, 0x00U};
        assert(length == sizeof trigger && memcmp(bytes, trigger, sizeof trigger) == 0);
    } else {
        assert(address == LCD_ADDRESS && owner == &lcd_owner && length == LCD_COLUMNS);
    }
    const i2c_async_result_t result = start(owner, address, length, token);
    if (result == I2C_ASYNC_ACCEPTED) {
        transfer.tx = bytes;
        memcpy(transfer.saved_tx, bytes, length);
    }
    return result;
}

static i2c_async_result_t rx(void *context, const void *owner, uint8_t address,
                            uint8_t *bytes, size_t length, i2c_async_token_t *token)
{
    (void)context;
    assert(address == DHT_ADDRESS && owner != &lcd_owner && (length == 1U || length == 7U));
    const i2c_async_result_t result = start(owner, address, length, token);
    if (result == I2C_ASYNC_ACCEPTED) {
        transfer.rx = bytes;
    }
    return result;
}

static void release_bus(void)
{
    transfer.owner = NULL;
    transfer.tx = NULL;
    transfer.rx = NULL;
}

static i2c_async_result_t poll(void *context, const void *owner, i2c_async_token_t token)
{
    (void)context;
    assert(transfer.owner == owner && transfer.token == token);
    if (transfer.tx != NULL) {
        assert(memcmp(transfer.tx, transfer.saved_tx, transfer.length) == 0);
    }
    if (now_ms < transfer.ready_ms) {
        return I2C_ASYNC_PENDING;
    }
    if ((transfer.address == DHT_ADDRESS && sensor_missing)
        || (transfer.address == LCD_ADDRESS && display_missing)) {
        release_bus();
        return I2C_ASYNC_ERROR;
    }
    if (transfer.rx != NULL) {
        uint8_t frame[] = {0x18U, 0x80U, 0U, 0x06U, 0U, 0U, 0U};
        assert(dht20_protocol_crc8(frame, 6U, &frame[6]) == APP_OK);
        memcpy(transfer.rx, frame, transfer.length);
        if (transfer.length == sizeof frame) {
            ++sensor_frames;
        }
    }
    release_bus();
    return I2C_ASYNC_COMPLETE;
}

static i2c_async_result_t cancel(void *context, const void *owner, i2c_async_token_t token)
{
    (void)context;
    assert(transfer.owner == owner && transfer.token == token);
    release_bus();
    return I2C_ASYNC_COMPLETE;
}

static app_status_t lcd_prepare(void *context, const i2c_async_t *bus, uint8_t address)
{
    (void)context;
    assert(bus->transmit == tx && address == LCD_ADDRESS);
    /* Overlap the first display transfer with DHT20's power-up status read. */
    return now_ms >= 500U ? APP_OK : APP_NOT_READY;
}

static app_status_t lcd_line(void *context, const i2c_async_t *bus, uint8_t address,
                             uint8_t row, const char *text, uint8_t columns)
{
    (void)context;
    static i2c_async_token_t token;
    static bool pending;
    assert(row < 3U && columns == LCD_COLUMNS && text[columns] == '\0');
    if (!pending) {
        const i2c_async_result_t result =
            bus->transmit(bus->context, &lcd_owner, address, (const uint8_t *)text, columns, &token);
        assert(result == I2C_ASYNC_ACCEPTED || result == I2C_ASYNC_BUSY);
        pending = result == I2C_ASYNC_ACCEPTED;
        return APP_BUSY;
    }
    const i2c_async_result_t result = bus->poll(bus->context, &lcd_owner, token);
    if (result == I2C_ASYNC_PENDING) {
        return APP_BUSY;
    }
    pending = false;
    if (result != I2C_ASYNC_COMPLETE) {
        return APP_IO_ERROR;
    }
    memcpy(screen[row], text, (size_t)columns + 1U);
    return APP_OK;
}

static void advance(unsigned ticks)
{
    for (unsigned i = 0U; i < ticks; ++i) {
        now_ms += 10U;
        SCH_Update();
        SCH_Dispatch_Tasks();
        check_gpio_safety();
    }
}

static void press(button_id_t id, unsigned ticks)
{
    pressed = BUTTON_MASK(id);
    advance(ticks);
    pressed = 0U;
    advance(6U);
}

static app_mode_t current_mode(void)
{
    app_mode_t mode;
    assert(fsm_mode_get(&mode) == APP_OK);
    return mode;
}

static void expect_pair(traffic_color_t road1, traffic_color_t road2)
{
    static const uint16_t road1_code[] = {1U << 5U, 1U << 3U, ROAD1_PINS};
    static const uint16_t road2_code[] = {1U << 10U, 1U << 4U, ROAD2_PINS};
    assert(gpio_b == (uint16_t)(road1_code[road1] | road2_code[road2]));
}

static void expect_draft(uint32_t red, uint32_t green, uint32_t yellow, traffic_color_t selected)
{
    fsm_tuning_view_t view;
    assert(fsm_tuning_get(&view) == APP_OK);
    assert(view.draft.red_ms == red && view.draft.green_ms == green && view.draft.yellow_ms == yellow);
    assert(view.selected == selected);
}

int main(void)
{
    const i2c_async_t bus = {NULL, tx, rx, poll, cancel};
    const lcd_config_t lcd_config = {
        3U, LCD_COLUMNS, LCD_ADDRESS, bus, NULL, lcd_prepare, lcd_line
    };
    const app_runtime_config_t app = {
        .buttons_enabled = true, .button_active_low_mask = BUTTON_MASK_ALL,
        .bus = &bus, .clock_ms = clock_ms, .lcd = &lcd_config
    };
    app_runtime_services_t services;
    assert(button_bind_io(button_level, button_clock_ms) == APP_OK);
    assert(traffic_light_bind_io(write_pins) == APP_OK);
    assert(app_runtime_start(&app, &services) == APP_OK);
    assert(services.sensor == APP_OK && services.display == APP_OK);

    assert(current_mode() == APP_MODE_AUTOMATIC);
    expect_pair(TRAFFIC_RED, TRAFFIC_GREEN);
    advance(299U);
    assert(app_runtime_start(&app, &services) == APP_BUSY && SCH_Now() == 299U);
    expect_pair(TRAFFIC_RED, TRAFFIC_GREEN);
    advance(1U);
    expect_pair(TRAFFIC_RED, TRAFFIC_YELLOW);
    assert(strcmp(screen[0], "Automatic           ") == 0);
    assert(strncmp(screen[2], "T:25.0C H:50.0%", 15U) == 0);
    assert(sensor_frames >= 1U && bus_contentions >= 1U);

    press(BUTTON_ID_1, 7U);
    assert(current_mode() == APP_MODE_MANUAL);
    expect_pair(TRAFFIC_RED, TRAFFIC_YELLOW);
    press(BUTTON_ID_2, 65U);
    expect_pair(TRAFFIC_GREEN, TRAFFIC_RED);
    advance(30U);
    assert(strcmp(screen[0], "Manual              ") == 0);

    press(BUTTON_ID_1, 7U);
    assert(current_mode() == APP_MODE_TUNING);
    expect_pair(TRAFFIC_RED, TRAFFIC_RED);
    expect_draft(5000U, 3000U, 2000U, TRAFFIC_RED);
    press(BUTTON_ID_4, 7U);
    expect_draft(5000U, 3000U, 2000U, TRAFFIC_RED);
    press(BUTTON_ID_3, 56U);
    expect_draft(7000U, 3000U, 2000U, TRAFFIC_RED);
    press(BUTTON_ID_2, 7U);
    expect_draft(7000U, 3000U, 2000U, TRAFFIC_GREEN);
    press(BUTTON_ID_3, 7U);
    expect_draft(7000U, 4000U, 2000U, TRAFFIC_GREEN);
    advance(30U);
    assert(strcmp(screen[0], "Tuning              ") == 0);
    assert(strcmp(screen[1], "Green 4000 ms       ") == 0);

    pressed = BUTTON_MASK(BUTTON_ID_2);
    for (unsigned i = 0U; i < 60U && current_mode() == APP_MODE_TUNING; ++i) {
        advance(1U);
    }
    assert(current_mode() == APP_MODE_AUTOMATIC);
    expect_pair(TRAFFIC_RED, TRAFFIC_GREEN);
    pressed = 0U;
    advance(399U);
    expect_pair(TRAFFIC_RED, TRAFFIC_GREEN);
    advance(1U);
    expect_pair(TRAFFIC_RED, TRAFFIC_YELLOW);
    advance(200U);
    expect_pair(TRAFFIC_RED, TRAFFIC_RED);
    advance(99U);
    expect_pair(TRAFFIC_RED, TRAFFIC_RED);
    advance(1U);
    expect_pair(TRAFFIC_GREEN, TRAFFIC_RED);

    for (unsigned i = 0U; i < 30U; ++i) {
        press(BUTTON_ID_1, 7U);
        assert(current_mode() == APP_MODE_MANUAL);
        press(BUTTON_ID_1, 7U);
        assert(current_mode() == APP_MODE_TUNING);
        press(BUTTON_ID_3, 7U);
        expect_draft(8000U, 4000U, 2000U, TRAFFIC_RED);
        press(BUTTON_ID_1, 7U);
        assert(current_mode() == APP_MODE_AUTOMATIC);
    }

    sensor_missing = true;
    display_missing = true;
    const unsigned frames_before = sensor_frames;
    expect_pair(TRAFFIC_RED, TRAFFIC_GREEN);
    advance(390U);
    expect_pair(TRAFFIC_RED, TRAFFIC_YELLOW);
    advance(200U);
    expect_pair(TRAFFIC_RED, TRAFFIC_RED);
    advance(100U);
    expect_pair(TRAFFIC_GREEN, TRAFFIC_RED);
    advance(1110U);
    assert(sensor_frames == frames_before);
    assert(current_mode() == APP_MODE_AUTOMATIC);
    display_missing = false;
    advance(200U);
    assert(strcmp(screen[0], "Automatic           ") == 0);
    assert(strncmp(screen[2], "Unavailable", 11U) == 0 || strncmp(screen[2], "Stale", 5U) == 0);
    sensor_missing = false;
    advance(1000U);
    assert(sensor_frames > frames_before && strncmp(screen[2], "T:25.0C H:50.0%", 15U) == 0);

    const unsigned long_run_before = gpio_transitions;
    advance(360000U);
    assert(gpio_transitions > long_run_before + 2000U);
    assert(current_mode() == APP_MODE_AUTOMATIC);
    puts("PASS: integrated boot, mode/tuning gestures, shared bus, faults/recovery and one-hour simulation");
    return 0;
}
