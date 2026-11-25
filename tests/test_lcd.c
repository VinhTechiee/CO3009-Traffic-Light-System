#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dht20_protocol.h"
#include "fsm_mode.h"
#include "lcd.h"
#include "lcd_display.h"

enum { COLUMNS = 20, LCD_ADDRESS_FIXTURE = 0x27, SENSOR_ADDRESS = 0x38 };
static uint32_t now_ms;
static app_mode_t mode = APP_MODE_AUTOMATIC;
static char screen[2][COLUMNS + 1];
static unsigned preparations, lcd_writes, lcd_attempts, sensor_busy, lcd_busy;
static bool freeze, fail_line;
static int lcd_owner;
static struct {
    const void *owner;
    i2c_async_token_t token;
    const uint8_t *tx;
    uint8_t *rx;
    size_t length;
    uint8_t snapshot[COLUMNS];
} transfer;

app_status_t fsm_mode_get(app_mode_t *out)
{
    *out = mode;
    return APP_OK;
}

app_status_t fsm_tuning_get(fsm_tuning_view_t *view)
{
    *view = (fsm_tuning_view_t){{1600, 500, 1000}, TRAFFIC_RED, FSM_TUNING_EDITING, APP_OK};
    return APP_OK;
}

static uint32_t clock_ms(void *context)
{
    (void)context;
    return now_ms;
}

static i2c_async_result_t start(const void *owner, size_t length, i2c_async_token_t *token)
{
    if (transfer.owner != NULL) {
        assert(transfer.owner != owner);
        if (owner == &lcd_owner) {
            ++lcd_busy;
        } else {
            ++sensor_busy;
        }
        return I2C_ASYNC_BUSY;
    }
    transfer.owner = owner;
    transfer.length = length;
    *token = ++transfer.token;
    return I2C_ASYNC_ACCEPTED;
}

static i2c_async_result_t tx(void *context, const void *owner, uint8_t address,
                            const uint8_t *data, size_t length, i2c_async_token_t *token)
{
    (void)context;
    assert((address == LCD_ADDRESS_FIXTURE && owner == &lcd_owner && length == COLUMNS)
        || (address == SENSOR_ADDRESS && owner != &lcd_owner && length == 3U && data[0] == 0xACU));
    const i2c_async_result_t result = start(owner, length, token);
    if (result == I2C_ASYNC_ACCEPTED) {
        transfer.tx = data;
        memcpy(transfer.snapshot, data, length);
    }
    return result;
}

static i2c_async_result_t rx(void *context, const void *owner, uint8_t address,
                            uint8_t *data, size_t length, i2c_async_token_t *token)
{
    (void)context;
    assert(owner != &lcd_owner && address == SENSOR_ADDRESS && (length == 1U || length == 7U));
    const i2c_async_result_t result = start(owner, length, token);
    if (result == I2C_ASYNC_ACCEPTED) {
        transfer.rx = data;
    }
    return result;
}

static i2c_async_result_t poll(void *context, const void *owner, i2c_async_token_t token)
{
    (void)context;
    assert(transfer.owner == owner && transfer.token == token);
    if (transfer.tx != NULL) {
        assert(memcmp(transfer.tx, transfer.snapshot, transfer.length) == 0);
    }
    if (owner == &lcd_owner && freeze) {
        return I2C_ASYNC_PENDING;
    }
    if (transfer.rx != NULL) {
        if (transfer.length == 1U) {
            transfer.rx[0] = 0x18U;
        } else {
            uint8_t frame[7] = {0x18U, 0x80U, 0U, 0x06U, 0U, 0U, 0U};
            assert(dht20_protocol_crc8(frame, 6, &frame[6]) == APP_OK);
            memcpy(transfer.rx, frame, sizeof frame);
        }
    }
    transfer.owner = NULL;
    transfer.tx = NULL;
    transfer.rx = NULL;
    if (owner == &lcd_owner && fail_line) {
        fail_line = false;
        return I2C_ASYNC_ERROR;
    }
    return I2C_ASYNC_COMPLETE;
}

static i2c_async_result_t cancel(void *context, const void *owner, i2c_async_token_t token)
{
    (void)context;
    assert(transfer.owner == owner && transfer.token == token);
    transfer.owner = NULL;
    transfer.tx = NULL;
    transfer.rx = NULL;
    return I2C_ASYNC_COMPLETE;
}

static app_status_t prepare(void *context, const i2c_async_t *bus, uint8_t address)
{
    (void)context;
    assert(bus->transmit == tx && address == LCD_ADDRESS_FIXTURE);
    return ++preparations == 1U ? APP_BUSY : APP_OK;
}

static app_status_t write_line(void *context, const i2c_async_t *bus, uint8_t address,
                               uint8_t row, const char *line, uint8_t columns)
{
    (void)context;
    static i2c_async_token_t token;
    static bool pending;
    assert(row < 2U && columns == COLUMNS && line[columns] == '\0');
    ++lcd_attempts;
    if (!pending) {
        const i2c_async_result_t result = bus->transmit(bus->context, &lcd_owner, address,
            (const uint8_t *)line, columns, &token);
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
    memcpy(screen[row], line, (size_t)columns + 1U);
    ++lcd_writes;
    return APP_OK;
}

static void run(unsigned steps, bool display)
{
    for (unsigned i = 0; i < steps; ++i) {
        now_ms += 10U;
        dht20_task();
        if (display) {
            lcd_display_task();
        }
        const unsigned before = lcd_attempts;
        lcd_task();
        assert(lcd_attempts - before <= 1U);
    }
}

int main(void)
{
    const i2c_async_t bus = {NULL, tx, rx, poll, cancel};
    lcd_config_t config = {2, COLUMNS, LCD_ADDRESS_FIXTURE, bus, NULL, prepare, write_line};
    const lcd_display_layout_t layout = {0, 1, 1};
    const lcd_display_layout_t invalid_layout = {0, 0, 1};
    const dht20_config_t sensor = {2000, 600, 150, 100, 1000, 0};
    assert(lcd_init() == APP_NOT_READY);
    assert(lcd_configure(NULL) == APP_INVALID_ARGUMENT);
    assert(lcd_configure(&config) == APP_OK);
    config.address_7bit = 0;
    assert(lcd_display_configure(&layout) == APP_NOT_READY);
    assert(lcd_init() == APP_OK);
    assert(lcd_init() == APP_BUSY);
    assert(lcd_display_configure(&invalid_layout) == APP_INVALID_ARGUMENT);
    assert(lcd_display_configure(&layout) == APP_OK);
    assert(dht20_configure(&bus, clock_ms, NULL, &sensor) == APP_OK);
    assert(dht20_init() == APP_OK);

    char input[] = "First";
    assert(lcd_request_line(0, input) == APP_OK);
    input[0] = 'X';
    assert(lcd_request_line(0, "too long for this row") == APP_NO_CAPACITY);
    assert(lcd_request_line(2, input) == APP_INVALID_ARGUMENT);
    freeze = true;
    run(53, false);
    assert(sensor_busy > 0);
    assert(lcd_request_line(0, "Next") == APP_OK);
    freeze = false;
    run(1, false);
    assert(strcmp(screen[0], "First               ") == 0);
    fail_line = true;
    run(4, false);
    assert(lcd_busy > 0);
    run(10, false);
    assert(strcmp(screen[0], "Next                ") == 0);

    for (unsigned i = 0; i < 40U && strncmp(screen[1], "T:", 2) != 0; ++i) {
        run(1, true);
    }
    assert(strcmp(screen[0], "Automatic           ") == 0);
    assert(strncmp(screen[1], "T:25.0C H:50.0%", 15) == 0);
    const unsigned writes_before = lcd_writes;
    run(5, true);
    assert(lcd_writes == writes_before);
    mode = APP_MODE_TUNING;
    run(8, true);
    assert(strcmp(screen[0], "Tuning              ") == 0);
    assert(strcmp(screen[1], "Red 1600 ms         ") == 0);
    mode = APP_MODE_MANUAL;
    run(110, true);
    assert(strcmp(screen[0], "Manual              ") == 0);
    assert(strcmp(screen[1], "Stale               ") == 0);
    assert(dht20_stop() == APP_OK);
    run(8, true);
    assert(strcmp(screen[1], "Unavailable         ") == 0);
    puts("LCD and shared DHT20 bus tests passed");
    return 0;
}
