#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "dht20.h"
#include "dht20_protocol.h"

typedef struct {
    uint32_t now_ms;
    bool active, foreign_busy, hold, aborting, delayed_abort, cancel_error;
    bool bad_crc, busy_frame, uncalibrated_frame, otp_bad_frame;
    bool needs_calibration, calibration_fails, alternate_frame;
    unsigned int submit_errors, poll_errors, pending_polls;
    unsigned int calls, submissions, completions, cancels, measurements;
    unsigned int cal_writes, cal_reads, restores, status_reads;
    unsigned int repair_position, restore_position, completed_repair_registers;
    uint8_t selected_register;
    uint32_t command_complete_ms, cal_complete_ms, cal_read_ms, status_complete_ms;
    uint32_t first_status_ms;
    const void *owner;
    i2c_async_token_t token;
    uint8_t *rx;
    const uint8_t *tx;
    uint8_t tx_snapshot[3];
    size_t length;
} fake_t;
static fake_t fake;
static const uint8_t registers[3] = {0x1Bu, 0x1Cu, 0x1Eu};
/* Deliberately distinct values detect reuse of another register's response. */
static const uint8_t repair_values[3][2] = {
    {0xA5u, 0x5Au}, {0x3Cu, 0xC3u}, {0x96u, 0x69u}
};

static uint32_t clock_ms(void *context)
{
    assert(context == &fake);
    return fake.now_ms;
}

static uint32_t alternative_clock_ms(void *context)
{
    return clock_ms(context);
}

static void verify_borrow(void)
{
    if (fake.active && fake.tx != NULL) {
        assert(memcmp(fake.tx, fake.tx_snapshot, fake.length) == 0);
    }
}

static i2c_async_result_t accept(const void *owner, uint8_t address,
                                size_t length, i2c_async_token_t *token)
{
    ++fake.calls;
    assert(owner != NULL && address == 0x38u && token != NULL);
    assert(length > 0u && length <= 7u);
    verify_borrow();
    if (fake.active) {
        assert(owner != fake.owner); /* no duplicate submission by same client */
        return I2C_ASYNC_BUSY;
    }
    if (fake.foreign_busy) {
        return I2C_ASYNC_BUSY;
    }
    if (fake.submit_errors > 0u) {
        --fake.submit_errors;
        return I2C_ASYNC_ERROR;
    }
    fake.active = true;
    fake.owner = owner;
    ++fake.token;
    *token = fake.token;
    fake.length = length;
    ++fake.submissions;
    return I2C_ASYNC_ACCEPTED;
}

static i2c_async_result_t transmit(void *context, const void *owner,
    uint8_t address, const uint8_t *data, size_t length, i2c_async_token_t *token)
{
    i2c_async_result_t result;
    assert(context == &fake && data != NULL && length == 3u);
    result = accept(owner, address, length, token);
    if (result == I2C_ASYNC_ACCEPTED) {
        fake.rx = NULL;
        fake.tx = data;
        memcpy(fake.tx_snapshot, data, length);
        if (data[0] == 0xACu) {
            assert(data[1] == 0x33u && data[2] == 0u);
            if (fake.status_reads > 0u) {
                assert((uint32_t)(fake.now_ms - fake.status_complete_ms) >= 10u);
            }
            ++fake.measurements;
        } else if ((data[0] & 0xF0u) == 0x10u) {
            assert(fake.repair_position < 3u);
            assert(data[0] == registers[fake.repair_position]);
            fake.selected_register = (uint8_t)fake.repair_position;
            ++fake.repair_position;
            assert(data[1] == 0u && data[2] == 0u);
            ++fake.cal_writes;
        } else {
            assert(fake.restore_position < 3u);
            assert(data[0] == (uint8_t)(0xB0u | registers[fake.restore_position]));
            assert(data[1] == repair_values[fake.restore_position][0]);
            assert(data[2] == repair_values[fake.restore_position][1]);
            ++fake.restore_position;
            assert((uint32_t)(fake.now_ms - fake.cal_read_ms) >= 10u);
            ++fake.restores;
        }
    }
    return result;
}

static i2c_async_result_t receive(void *context, const void *owner,
    uint8_t address, uint8_t *data, size_t length, i2c_async_token_t *token)
{
    i2c_async_result_t result;
    assert(context == &fake && data != NULL);
    result = accept(owner, address, length, token);
    if (result == I2C_ASYNC_ACCEPTED) {
        fake.rx = data;
        fake.tx = NULL;
        if (length == 1u) {
            if (fake.status_reads == 0u) {
                fake.first_status_ms = fake.now_ms;
            }
            if (fake.completed_repair_registers == 3u) {
                assert((uint32_t)(fake.now_ms - fake.cal_complete_ms) >= 10u);
            }
            ++fake.status_reads;
        } else if (length == 3u) {
            assert((uint32_t)(fake.now_ms - fake.cal_complete_ms) >= 5u);
            ++fake.cal_reads;
        } else {
            assert(length == 7u);
            assert((uint32_t)(fake.now_ms - fake.command_complete_ms) >= 80u);
        }
    }
    return result;
}

static i2c_async_result_t poll(void *context, const void *owner,
                              i2c_async_token_t token)
{
    ++fake.calls;
    assert(context == &fake);
    if (!fake.active || owner != fake.owner || token != fake.token) {
        return I2C_ASYNC_ERROR;
    }
    verify_borrow();
    if (fake.aborting) {
        if (fake.pending_polls > 0u) {
            --fake.pending_polls;
            return I2C_ASYNC_PENDING;
        }
        fake.active = false;
        fake.aborting = false;
        return fake.cancel_error ? I2C_ASYNC_ERROR : I2C_ASYNC_COMPLETE;
    }
    if (fake.hold) {
        return I2C_ASYNC_PENDING;
    }
    if (fake.poll_errors > 0u) {
        --fake.poll_errors;
        fake.active = false;
        return I2C_ASYNC_ERROR;
    }
    if (fake.rx != NULL) {
        if (fake.length == 1u) {
            fake.rx[0] = fake.needs_calibration
                && (fake.completed_repair_registers < 3u || fake.calibration_fails)
                ? 0x10u : 0x18u;
            if ((fake.rx[0] & 0x18u) != 0x18u) {
                fake.repair_position = 0u;
                fake.restore_position = 0u;
                fake.completed_repair_registers = 0u;
            }
            fake.status_complete_ms = fake.now_ms;
        } else if (fake.length == 3u) {
            fake.rx[0] = 0u;
            fake.rx[1] = repair_values[fake.selected_register][0];
            fake.rx[2] = repair_values[fake.selected_register][1];
            fake.cal_read_ms = fake.now_ms;
        } else {
            const uint8_t frame[7] = {0x18u, 0x80u, 0u, 0x06u, 0u, 0u, 0x23u};
            memcpy(fake.rx, frame, sizeof frame);
            if (fake.alternate_frame) {
                /* Synthetic 25% RH, 0 degrees C; independent fixed payload. */
                const uint8_t alternative[6] = {0x18u, 0x40u, 0u, 0x04u, 0u, 0u};
                memcpy(fake.rx, alternative, sizeof alternative);
            }
            if (fake.busy_frame) { fake.rx[0] |= 0x80u; }
            if (fake.uncalibrated_frame) { fake.rx[0] &= (uint8_t)~0x08u; }
            if (fake.otp_bad_frame) { fake.rx[0] &= (uint8_t)~0x10u; }
            assert(dht20_protocol_crc8(fake.rx, 6u, &fake.rx[6]) == APP_OK);
            if (fake.bad_crc) { fake.rx[6] ^= 1u; }
        }
    } else if (fake.tx[0] == 0xACu) {
        fake.command_complete_ms = fake.now_ms;
    } else {
        fake.cal_complete_ms = fake.now_ms;
        if ((fake.tx[0] & 0xF0u) == 0xB0u) {
            ++fake.completed_repair_registers;
        }
    }
    fake.active = false;
    ++fake.completions;
    return I2C_ASYNC_COMPLETE;
}

static i2c_async_result_t cancel(void *context, const void *owner,
                                i2c_async_token_t token)
{
    ++fake.calls;
    assert(context == &fake);
    if (!fake.active || owner != fake.owner || token != fake.token) {
        return I2C_ASYNC_ERROR;
    }
    verify_borrow();
    ++fake.cancels;
    if (fake.delayed_abort) {
        fake.aborting = true;
        fake.pending_polls = 3u;
        return I2C_ASYNC_PENDING;
    }
    fake.active = false;
    return fake.cancel_error ? I2C_ASYNC_ERROR : I2C_ASYNC_COMPLETE;
}

static i2c_async_t transport(void)
{
    const i2c_async_t bus = {&fake, transmit, receive, poll, cancel};
    return bus;
}

static dht20_config_t configuration(void)
{
    const dht20_config_t config = {2000u, 100u, 400u, 30u, 3000u, 1u};
    return config;
}

static dht20_info_t info(void)
{
    dht20_info_t value;
    assert(dht20_get_info(&value) == APP_OK);
    return value;
}

static void step(uint32_t milliseconds)
{
    const unsigned int calls = fake.calls;
    fake.now_ms += milliseconds;
    dht20_task();
    assert(fake.calls - calls <= 1u); /* enforce per-invocation backend bound */
    verify_borrow();
}

static void until(dht20_state_t state)
{
    unsigned int i;
    for (i = 0u; i < 2000u && info().state != state; ++i) { step(10u); }
    assert(info().state == state);
}

static void setup(uint8_t retries)
{
    i2c_async_t bus;
    dht20_config_t config = configuration();
    if (dht20_stop() == APP_OK) { until(DHT20_UNINITIALIZED); }
    memset(&fake, 0, sizeof fake);
    bus = transport();
    config.max_retries = retries;
    assert(dht20_configure(&bus, clock_ms, &fake, &config) == APP_OK);
    /* Changing caller copies must not change driver dependencies/settings. */
    bus.receive = NULL;
    config.transfer_timeout_ms = 0u;
    assert(dht20_init() == APP_OK);
}

static void first_sample(void)
{
    dht20_sample_t sample;
    until(DHT20_IDLE);
    until(DHT20_MEASUREMENT_REQUEST);
    until(DHT20_IDLE);
    assert(dht20_get_sample(&sample) == APP_OK);
    assert(sample.valid && sample.temperature_c == 25.0f);
    assert(sample.relative_humidity_percent == 50.0f);
}

static void test_initialization_and_measurement(void)
{
    dht20_sample_t sample = {11.0f, 22.0f, false};
    unsigned int i;
    assert(dht20_init() == APP_NOT_READY);
    assert(dht20_get_sample(&sample) == APP_NOT_READY);
    assert(dht20_get_info(NULL) == APP_INVALID_ARGUMENT);
    assert(dht20_get_sample(NULL) == APP_INVALID_ARGUMENT);
    setup(1u);
    assert(info().sample_status == DHT20_SAMPLE_NEVER);
    assert(dht20_get_sample(&sample) == APP_NOT_READY);
    assert(sample.temperature_c == 11.0f && sample.relative_humidity_percent == 22.0f);
    for (i = 0u; i < 1000u; ++i) { step(0u); }
    assert(fake.submissions == 0u);
    step(499u);
    assert(fake.submissions == 0u);
    first_sample();
    assert(fake.first_status_ms >= 500u);
    assert(fake.cal_writes == 0u && fake.measurements == 1u);
    assert(info().sample_age_ms == 10u); /* one task step since reception */
    assert((uint32_t)(fake.now_ms - info().last_success_ms) == 10u);
    for (i = 0u; i < 3u; ++i) {
        until(DHT20_MEASUREMENT_REQUEST);
        until(DHT20_IDLE);
        assert(dht20_get_sample(&sample) == APP_OK);
    }
    assert(fake.measurements == 4u);
}

static void test_calibration(void)
{
    setup(0u);
    fake.needs_calibration = true;
    first_sample();
    assert(fake.cal_writes == 3u && fake.cal_reads == 3u && fake.restores == 3u);
    assert(fake.status_reads == 2u);
    setup(0u);
    fake.needs_calibration = true;
    fake.calibration_fails = true;
    until(DHT20_UNAVAILABLE);
    assert(info().last_error == DHT20_ERROR_CALIBRATION);
    assert(info().retry_exhausted && fake.measurements == 0u);
}

static void test_frame_errors(void)
{
    setup(0u);
    fake.bad_crc = true;
    until(DHT20_UNAVAILABLE);
    assert(info().last_error == DHT20_ERROR_CRC);
    setup(0u);
    fake.uncalibrated_frame = true;
    until(DHT20_UNAVAILABLE);
    assert(info().last_error == DHT20_ERROR_CALIBRATION);
    setup(0u);
    fake.otp_bad_frame = true;
    until(DHT20_UNAVAILABLE);
    assert(info().last_error == DHT20_ERROR_OTP);
    setup(0u);
    fake.busy_frame = true;
    until(DHT20_UNAVAILABLE);
    assert(info().last_error == DHT20_ERROR_SENSOR_BUSY);
    assert(fake.measurements == 1u); /* busy polling never retriggers */
    setup(0u);
    fake.busy_frame = true;
    until(DHT20_VALIDATE);
    step(10u); /* first busy frame -> retry read */
    fake.busy_frame = false;
    until(DHT20_IDLE);
    assert(info().last_error == DHT20_ERROR_NONE);
    assert(fake.measurements == 1u);
}

static void test_transient_failure_preserves_sample(void)
{
    dht20_sample_t sample;
    uint32_t last_success;
    unsigned int calls, i;
    setup(1u);
    first_sample();
    last_success = info().last_success_ms;
    fake.bad_crc = true;
    until(DHT20_ERROR_RECOVERY);
    assert(info().sample_status == DHT20_SAMPLE_STALE);
    assert(dht20_get_sample(&sample) == APP_NOT_READY);
    assert(sample.valid && sample.temperature_c == 25.0f);
    assert(sample.relative_humidity_percent == 50.0f);
    assert(info().last_success_ms == last_success);
    fake.bad_crc = false;
    until(DHT20_CONVERSION_WAIT);
    calls = fake.calls;
    for (i = 0u; i < 1000u; ++i) { step(0u); }
    assert(fake.calls == calls && info().state == DHT20_CONVERSION_WAIT);
    until(DHT20_IDLE);
    assert(dht20_get_sample(&sample) == APP_OK);
    assert(info().last_success_ms != last_success && info().retries_used == 0u);
}

static void test_bus_errors_and_retries(void)
{
    unsigned int submissions;
    setup(1u);
    fake.foreign_busy = true;
    until(DHT20_ERROR_RECOVERY);
    assert(info().last_error == DHT20_ERROR_BUS_BUSY && fake.cancels == 0u);
    fake.foreign_busy = false;
    first_sample();
    assert(!info().retry_exhausted && info().retries_used == 0u);
    setup(1u);
    fake.submit_errors = 2u;
    until(DHT20_UNAVAILABLE);
    assert(info().last_error == DHT20_ERROR_TRANSFER);
    assert(info().retries_used == 1u && fake.submit_errors == 0u);
    submissions = fake.submissions;
    step(100000u);
    assert(fake.submissions == submissions); /* exhaustion is latched */
    assert(dht20_init() == APP_OK);
    first_sample();
    setup(0u);
    fake.poll_errors = 1u;
    until(DHT20_UNAVAILABLE);
    assert(info().last_error == DHT20_ERROR_TRANSFER && fake.cancels == 0u);
}

static void test_timeout_and_cancel(void)
{
    unsigned int i, submissions;
    setup(0u);
    fake.hold = true;
    fake.delayed_abort = true;
    until(DHT20_CANCEL_REQUEST);
    assert(info().last_error == DHT20_ERROR_TIMEOUT && fake.active);
    submissions = fake.submissions;
    step(10u);
    assert(fake.cancels == 1u && info().state == DHT20_CANCEL_WAIT);
    assert(dht20_configure(&(i2c_async_t){&fake, transmit, receive, poll, cancel},
        clock_ms, &fake, &(dht20_config_t){2000u, 100u, 400u, 30u, 3000u, 1u}) == APP_BUSY);
    for (i = 0u; i < 3u; ++i) {
        step(10u);
        assert(fake.active && fake.submissions == submissions);
    }
    step(10u);
    assert(!fake.active && info().state == DHT20_UNAVAILABLE);
    setup(0u);
    fake.hold = true;
    fake.delayed_abort = true;
    until(DHT20_TRANSFER_WAIT);
    assert(dht20_init() == APP_OK);
    step(10u);
    assert(dht20_init() == APP_OK); /* restart during abort does not cancel twice */
    until(DHT20_POWER_UP_WAIT);
    assert(fake.cancels == 1u);
    fake.hold = false;
    first_sample();
    until(DHT20_MEASUREMENT_REQUEST);
    step(10u); /* outstanding TX */
    assert(dht20_stop() == APP_OK);
    until(DHT20_UNINITIALIZED);
    assert(!fake.active && fake.cancels == 2u);
}

static void test_sample_age_and_rollover(void)
{
    dht20_sample_t before, after;
    uint32_t stamp, initial_age;
    setup(0u);
    first_sample();
    assert(dht20_get_sample(&before) == APP_OK);
    stamp = info().last_success_ms;
    initial_age = info().sample_age_ms;
    fake.now_ms += 3000u; /* getters must age even without task dispatch */
    assert(info().sample_status == DHT20_SAMPLE_STALE);
    assert(info().sample_age_ms == initial_age + 3000u);
    assert(info().last_success_ms == stamp);
    assert(dht20_get_sample(&after) == APP_NOT_READY);
    assert(after.temperature_c == before.temperature_c && after.valid);
    fake.bad_crc = true;
    until(DHT20_MEASUREMENT_REQUEST);
    until(DHT20_UNAVAILABLE);
    assert(dht20_get_sample(&after) == APP_IO_ERROR);
    assert(after.temperature_c == before.temperature_c);
    assert(after.relative_humidity_percent == before.relative_humidity_percent);
    assert(after.valid && info().last_success_ms == stamp);
    setup(0u);
    fake.now_ms = UINT32_MAX - 200u;
    assert(dht20_init() == APP_OK);
    first_sample(); /* power-up wait crosses wrap */
    fake.now_ms = UINT32_MAX - 40u;
    assert(dht20_init() == APP_OK);
    first_sample();
    /* Accumulated age stays stale over full elapsed-time cycles. */
    initial_age = info().sample_age_ms;
    fake.now_ms += UINT32_C(0x80000000);
    assert(info().sample_age_ms == initial_age + UINT32_C(0x80000000));
    fake.now_ms += UINT32_C(0x80000000);
    assert(info().sample_age_ms == UINT32_MAX);
    assert(info().sample_status == DHT20_SAMPLE_STALE);
}

static void test_foreign_owner_and_rollover_deadlines(void)
{
    static const uint8_t other_owner = 0u;
    static const uint8_t wrong_owner = 0u;
    const uint8_t other_tx[3] = {0xACu, 0x33u, 0u};
    i2c_async_token_t other_token = 0u;
    unsigned int cancellations;
    setup(0u);
    assert(transmit(&fake, &other_owner, 0x38u, other_tx, 3u, &other_token)
           == I2C_ASYNC_ACCEPTED);
    assert(cancel(&fake, &wrong_owner, other_token) == I2C_ASYNC_ERROR);
    assert(cancel(&fake, &other_owner, other_token + 1u) == I2C_ASYNC_ERROR);
    assert(fake.active);
    cancellations = fake.cancels;
    until(DHT20_UNAVAILABLE);
    assert(info().last_error == DHT20_ERROR_BUS_BUSY);
    assert(fake.active && fake.owner == &other_owner);
    assert(fake.cancels == cancellations); /* never abort another client's token */
    assert(poll(&fake, &other_owner, other_token) == I2C_ASYNC_COMPLETE);
    assert(dht20_init() == APP_OK);
    first_sample();

    /* Start conversion near wrap and verify the 80 ms wait across zero. */
    fake.now_ms = UINT32_MAX - 100u;
    until(DHT20_MEASUREMENT_REQUEST);
    until(DHT20_IDLE);
    assert(info().last_error == DHT20_ERROR_NONE);
    assert(fake.now_ms < 1000u);

    /* Start an accepted transfer near wrap, then exercise its own deadline. */
    fake.now_ms = UINT32_MAX - 50u;
    until(DHT20_MEASUREMENT_REQUEST);
    fake.hold = true;
    until(DHT20_CANCEL_REQUEST);
    assert(info().last_error == DHT20_ERROR_TIMEOUT && fake.now_ms < 1000u);
    until(DHT20_UNAVAILABLE);
}

static void test_invalid_configuration(void)
{
    i2c_async_t bus = transport();
    dht20_config_t config = configuration();
    dht20_config_t bad;
    assert(dht20_stop() == APP_OK);
    until(DHT20_UNINITIALIZED);
    assert(dht20_configure(NULL, clock_ms, &fake, &config) == APP_INVALID_ARGUMENT);
    assert(dht20_configure(&bus, NULL, &fake, &config) == APP_INVALID_ARGUMENT);
    assert(dht20_configure(&bus, clock_ms, &fake, NULL) == APP_INVALID_ARGUMENT);
    bus.transmit = NULL;
    assert(dht20_configure(&bus, clock_ms, &fake, &config) == APP_INVALID_ARGUMENT);
    bus = transport(); bus.receive = NULL;
    assert(dht20_configure(&bus, clock_ms, &fake, &config) == APP_INVALID_ARGUMENT);
    bus = transport(); bus.poll = NULL;
    assert(dht20_configure(&bus, clock_ms, &fake, &config) == APP_INVALID_ARGUMENT);
    bus = transport(); bus.cancel = NULL;
    assert(dht20_configure(&bus, clock_ms, &fake, &config) == APP_INVALID_ARGUMENT);
    bus = transport();
    bad = config; bad.measurement_interval_ms = 0u;
    assert(dht20_configure(&bus, clock_ms, &fake, &bad) == APP_INVALID_ARGUMENT);
    bad = config; bad.transfer_timeout_ms = 0u;
    assert(dht20_configure(&bus, clock_ms, &fake, &bad) == APP_INVALID_ARGUMENT);
    bad = config; bad.conversion_timeout_ms = 80u;
    assert(dht20_configure(&bus, clock_ms, &fake, &bad) == APP_INVALID_ARGUMENT);
    bad = config; bad.retry_delay_ms = UINT32_MAX;
    assert(dht20_configure(&bus, clock_ms, &fake, &bad) == APP_INVALID_ARGUMENT);
    bad = config; bad.stale_after_ms = 0u;
    assert(dht20_configure(&bus, clock_ms, &fake, &bad) == APP_INVALID_ARGUMENT);
    assert(info().state == DHT20_UNINITIALIZED);
}

static void test_delayed_frame_age(void)
{
    uint32_t received;
    dht20_sample_t sample;
    setup(0u);
    until(DHT20_VALIDATE);
    received = fake.now_ms;
    step(10000u);
    assert(info().last_success_ms == received);
    assert(info().sample_age_ms == 10000u);
    assert(info().sample_status == DHT20_SAMPLE_STALE);
    assert(dht20_get_sample(&sample) == APP_NOT_READY);
    assert(sample.valid && sample.temperature_c == 25.0f);
    assert(sample.relative_humidity_percent == 50.0f);
}

static void test_reconfigure_age(void)
{
    i2c_async_t bus;
    dht20_config_t config = configuration();
    uint32_t stamp, age;
    unsigned int i;
    setup(0u);
    first_sample();
    stamp = info().last_success_ms;
    age = info().sample_age_ms;
    assert(dht20_stop() == APP_OK);
    fake.now_ms += 5000u; /* no getter/task to account for this interval */
    bus = transport();
    assert(dht20_configure(&bus, clock_ms, &fake, &config) == APP_OK);
    assert(info().last_success_ms == stamp);
    assert(info().sample_age_ms == age + 5000u);
    assert(info().sample_status != DHT20_SAMPLE_FRESH);
    assert(dht20_configure(&bus, alternative_clock_ms, &fake, &config)
           == APP_INVALID_ARGUMENT);
    assert(dht20_configure(&bus, clock_ms, NULL, &config) == APP_INVALID_ARGUMENT);
    assert(info().last_success_ms == stamp && info().sample_age_ms == age + 5000u);
    for (i = 0u; i < 5u; ++i) {
        const uint32_t previous_age = info().sample_age_ms;
        fake.now_ms += 1000u;
        config.measurement_interval_ms += 100u;
        config.max_retries = (uint8_t)i;
        config.stale_after_ms = i % 2u == 0u ? 1000u : 20000u;
        assert(dht20_configure(&bus, clock_ms, &fake, &config) == APP_OK);
        assert(info().sample_age_ms == previous_age + 1000u);
        assert(info().last_success_ms == stamp);
        assert(info().sample_status != DHT20_SAMPLE_FRESH);
    }
    assert(dht20_init() == APP_OK);
    assert(info().sample_status == DHT20_SAMPLE_STALE);
    first_sample();
    assert(info().sample_status == DHT20_SAMPLE_FRESH);

    setup(0u);
    fake.now_ms = UINT32_MAX - 800u;
    assert(dht20_init() == APP_OK);
    first_sample();
    assert(fake.now_ms > UINT32_MAX - 500u);
    stamp = info().last_success_ms;
    age = info().sample_age_ms;
    assert(dht20_stop() == APP_OK);
    fake.now_ms += 5000u;
    assert(fake.now_ms < 5000u);
    config = configuration();
    assert(dht20_configure(&bus, clock_ms, &fake, &config) == APP_OK);
    assert(info().sample_age_ms == age + 5000u);
    assert(info().last_success_ms == stamp);
    assert(info().sample_status != DHT20_SAMPLE_FRESH);
}

static void test_delayed_invalid_preserves_sample(void)
{
    dht20_sample_t before, after;
    uint32_t stamp;
    setup(0u);
    first_sample();
    assert(dht20_get_sample(&before) == APP_OK);
    stamp = info().last_success_ms;
    fake.bad_crc = true;
    fake.alternate_frame = true; /* invalid payload would visibly change values */
    until(DHT20_VALIDATE);
    step(10000u);
    assert(info().state == DHT20_UNAVAILABLE);
    assert(info().last_error == DHT20_ERROR_CRC);
    assert(info().last_success_ms == stamp);
    assert(dht20_get_sample(&after) == APP_IO_ERROR);
    assert(after.valid == before.valid);
    assert(after.temperature_c == before.temperature_c);
    assert(after.relative_humidity_percent == before.relative_humidity_percent);
}

static void test_stale_latch_and_buffer_rollover(void)
{
    uint32_t received;
    setup(0u);
    first_sample();
    fake.now_ms += 2999u - info().sample_age_ms;
    assert(info().sample_age_ms == 2999u);
    assert(info().sample_status == DHT20_SAMPLE_FRESH);
    fake.now_ms += 1u;
    assert(info().sample_age_ms == 3000u);
    assert(info().sample_status == DHT20_SAMPLE_STALE);
    fake.now_ms += UINT32_C(0x80000000);
    assert(info().sample_status == DHT20_SAMPLE_STALE);
    fake.now_ms += UINT32_C(0x80000000);
    assert(info().sample_status == DHT20_SAMPLE_STALE);
    assert(info().sample_age_ms == UINT32_MAX);
    until(DHT20_MEASUREMENT_REQUEST);
    until(DHT20_IDLE);
    assert(info().sample_status == DHT20_SAMPLE_FRESH);

    setup(0u);
    fake.now_ms = UINT32_MAX - 800u;
    assert(dht20_init() == APP_OK);
    until(DHT20_VALIDATE);
    received = fake.now_ms;
    assert(received > UINT32_MAX - 500u);
    step(10000u); /* buffered frame crosses zero before validation */
    assert(info().last_success_ms == received && info().sample_age_ms == 10000u);
    assert(info().sample_status == DHT20_SAMPLE_STALE);

    setup(0u);
    until(DHT20_VALIDATE);
    received = fake.now_ms;
    fake.now_ms += UINT32_C(0x80000000);
    (void)info(); /* observe and accumulate age while validation stays pending */
    fake.now_ms += UINT32_C(0x80000000);
    (void)info();
    step(0u);
    assert(info().last_success_ms == received && info().sample_age_ms == UINT32_MAX);
    assert(info().sample_status == DHT20_SAMPLE_STALE);
}

static void test_repair_retry_and_new_readings(void)
{
    dht20_sample_t sample;
    setup(1u);
    fake.needs_calibration = true;
    until(DHT20_CAL_RESTORE_REQUEST); /* first register */
    step(10u);
    until(DHT20_CAL_WRITE_REQUEST); /* second register */
    step(10u);
    until(DHT20_CAL_READ_REQUEST);
    step(10u); /* second register receive accepted */
    fake.poll_errors = 1u;
    step(10u);
    assert(info().state == DHT20_ERROR_RECOVERY);
    assert(info().last_error == DHT20_ERROR_TRANSFER);
    first_sample();
    assert(fake.cal_writes == 5u && fake.restores == 4u);
    assert(fake.completed_repair_registers == 3u);
    assert(info().retries_used == 0u);
    fake.alternate_frame = true;
    until(DHT20_MEASUREMENT_REQUEST);
    until(DHT20_IDLE);
    assert(dht20_get_sample(&sample) == APP_OK);
    assert(sample.valid && sample.temperature_c == 0.0f);
    assert(sample.relative_humidity_percent == 25.0f);
}

static void test_retired_token_and_cancel_controls(void)
{
    const void *owner;
    i2c_async_token_t retired, current;
    unsigned int cancels;
    setup(0u);
    until(DHT20_TRANSFER_WAIT);
    owner = fake.owner;
    retired = fake.token;
    first_sample();
    until(DHT20_MEASUREMENT_REQUEST);
    step(10u);
    current = fake.token;
    assert(fake.active && current != retired);
    assert(poll(&fake, owner, retired) == I2C_ASYNC_ERROR);
    assert(cancel(&fake, owner, retired) == I2C_ASYNC_ERROR);
    assert(fake.active && fake.token == current);

    fake.hold = true;
    fake.delayed_abort = true;
    assert(dht20_stop() == APP_OK);
    step(10u);
    assert(info().state == DHT20_CANCEL_WAIT);
    cancels = fake.cancels;
    assert(dht20_stop() == APP_OK);
    assert(dht20_init() == APP_OK);
    assert(dht20_stop() == APP_OK);
    assert(dht20_init() == APP_OK); /* final request wins, abort stays pending */
    fake.cancel_error = true; /* terminal ERROR still guarantees quiescence */
    until(DHT20_POWER_UP_WAIT);
    assert(!fake.active && fake.cancels == cancels);
    fake.hold = false;
    fake.cancel_error = false;
    first_sample();

    until(DHT20_MEASUREMENT_REQUEST);
    step(10u);
    fake.delayed_abort = false;
    fake.cancel_error = true;
    assert(dht20_stop() == APP_OK);
    until(DHT20_UNINITIALIZED);
    assert(!fake.active);
}

static void test_exact_timeout_boundaries(void)
{
    uint32_t started;
    unsigned int i;
    setup(0u);
    fake.hold = true;
    until(DHT20_TRANSFER_WAIT);
    started = fake.now_ms;
    step(99u);
    assert(info().state == DHT20_TRANSFER_WAIT && fake.active);
    step(1u);
    assert((uint32_t)(fake.now_ms - started) == 100u);
    assert(info().state == DHT20_CANCEL_REQUEST);
    assert(info().last_error == DHT20_ERROR_TIMEOUT);
    until(DHT20_UNAVAILABLE);

    setup(0u);
    fake.foreign_busy = true;
    until(DHT20_STATUS_REQUEST);
    step(99u);
    assert(info().state == DHT20_STATUS_REQUEST && !fake.active);
    step(1u);
    assert(info().state == DHT20_UNAVAILABLE);
    assert(info().last_error == DHT20_ERROR_BUS_BUSY && fake.cancels == 0u);

    setup(0u);
    fake.busy_frame = true;
    until(DHT20_CONVERSION_WAIT);
    started = fake.now_ms;
    for (i = 0u; i < 60u; ++i) {
        if (info().state == DHT20_READ_REQUEST
            && (uint32_t)(fake.now_ms - started) >= 320u) {
            break;
        }
        step(10u);
    }
    assert(i < 60u && (uint32_t)(fake.now_ms - started) < 399u);
    step(399u - (uint32_t)(fake.now_ms - started));
    assert(info().state == DHT20_TRANSFER_WAIT); /* 399 < conversion deadline */
    step(0u);
    step(0u); /* validate busy frame */
    assert(info().state == DHT20_READ_REQUEST);
    step(1u);
    assert(info().state == DHT20_UNAVAILABLE);
    assert(info().last_error == DHT20_ERROR_SENSOR_BUSY);
}

int main(void)
{
    test_initialization_and_measurement();
    puts("PASS: startup, parser integration, repeated measurements, bounded calls");
    test_calibration();
    puts("PASS: exact register repair/read-back sequence and initialization failure");
    test_frame_errors();
    puts("PASS: CRC, calibration, OTP, sensor busy recovery and deadline");
    test_transient_failure_preserves_sample();
    puts("PASS: last-good values during transient failure and nonblocking conversion wait");
    test_bus_errors_and_retries();
    puts("PASS: shared-bus contention, transfer errors, bounded retries, restart");
    test_timeout_and_cancel();
    puts("PASS: transfer timeout, delayed cancellation, buffer ownership, stop");
    test_sample_age_and_rollover();
    puts("PASS: last-good preservation, stale/unavailable status, rollover, age saturation");
    test_foreign_owner_and_rollover_deadlines();
    puts("PASS: foreign/stale token isolation, conversion and transfer deadlines across wrap");
    test_invalid_configuration();
    puts("PASS: invalid configuration and null dependencies");
    test_delayed_frame_age();
    puts("PASS A: delayed valid frame publishes with reception age 10000 ms, stale");
    test_reconfigure_age();
    puts("PASS B/C/E: reconfiguration preserves timestamp and increasing age, including rollover");
    test_delayed_invalid_preserves_sample();
    puts("PASS D: delayed invalid frame preserves last-good sample and timestamp");
    test_stale_latch_and_buffer_rollover();
    puts("PASS F: stale remains stale until a new success; buffered rollover/saturation");
    test_repair_retry_and_new_readings();
    puts("PASS: mid-repair error, complete retry, distinct register values and measurements");
    test_retired_token_and_cancel_controls();
    puts("PASS: retired token isolation, stop/restart during abort, terminal cancel errors");
    test_exact_timeout_boundaries();
    puts("PASS: exact transfer/acquisition/conversion timeout boundaries");
    puts("All DHT20 driver tests passed.");
    return 0;
}
