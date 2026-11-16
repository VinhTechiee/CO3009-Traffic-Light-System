#include "dht20.h"
#include "dht20_protocol.h"
#include <limits.h>

/* Provenance: docs/dht20-driver.md. All waits start after observed completion. */
#define DHT20_ADDRESS_7BIT UINT8_C(0x38)
#define DHT20_POWER_UP_MS UINT32_C(500)
#define DHT20_CONVERSION_MS UINT32_C(80)

typedef struct {
    i2c_async_t bus;
    dht20_clock_ms_fn_t clock_ms;
    void *clock_context;
    dht20_config_t config;
    bool configured, active, restart, stopping, repaired, exhausted;
    bool has_sample, forced_stale, age_stale, frame_pending;
    dht20_state_t state, after_transfer;
    dht20_error_t error;
    i2c_async_token_t token;
    uint8_t tx[3], rx[7], reg_index;
    uint16_t retries;
    uint32_t since_ms, request_ms, conversion_ms, last_success_ms;
    uint32_t age_ms, age_observed_ms;
    uint32_t frame_received_ms, frame_age_ms;
    dht20_sample_t sample;
} dht20_driver_t;

/* Original no-argument APIs require one instance; group all private state. */
static dht20_driver_t dht20_driver;
static const uint8_t dht20_registers[3] = {0x1Bu, 0x1Cu, 0x1Eu};

static bool dht20_elapsed(uint32_t now, uint32_t start, uint32_t interval)
{
    return (uint32_t)(now - start) >= interval;
}

static uint32_t dht20_add_age(uint32_t age, uint32_t delta)
{
    return UINT32_MAX - age < delta ? UINT32_MAX : age + delta;
}

static void dht20_update_age(uint32_t now)
{
    const uint32_t delta = (uint32_t)(now - dht20_driver.age_observed_ms);
    if (dht20_driver.has_sample) {
        dht20_driver.age_ms = dht20_add_age(dht20_driver.age_ms, delta);
        if (dht20_driver.age_ms >= dht20_driver.config.stale_after_ms) {
            dht20_driver.age_stale = true;
        }
    }
    if (dht20_driver.frame_pending) {
        dht20_driver.frame_age_ms = dht20_add_age(dht20_driver.frame_age_ms, delta);
    }
    dht20_driver.age_observed_ms = now;
}

static void dht20_power_up(uint32_t now)
{
    dht20_driver.state = DHT20_POWER_UP_WAIT;
    dht20_driver.since_ms = now;
    dht20_driver.reg_index = 0u;
    dht20_driver.repaired = false;
    dht20_driver.restart = false;
    dht20_driver.frame_pending = false;
}

static void dht20_fail(dht20_error_t error, uint32_t now)
{
    dht20_driver.error = error;
    dht20_driver.forced_stale = true;
    dht20_driver.frame_pending = false;
    dht20_driver.since_ms = now;
    if (dht20_driver.active) {
        dht20_driver.state = DHT20_CANCEL_REQUEST;
    } else if (dht20_driver.retries >= dht20_driver.config.max_retries) {
        dht20_driver.exhausted = true;
        dht20_driver.state = DHT20_UNAVAILABLE;
    } else {
        ++dht20_driver.retries;
        dht20_driver.state = DHT20_ERROR_RECOVERY;
    }
}

static bool dht20_interval_valid(uint32_t interval)
{
    return interval != 0u && interval <= (uint32_t)INT32_MAX;
}

app_status_t dht20_configure(const i2c_async_t *transport,
    dht20_clock_ms_fn_t clock_ms, void *clock_context,
    const dht20_config_t *config)
{
    if (transport == NULL || config == NULL || clock_ms == NULL
        || transport->transmit == NULL || transport->receive == NULL
        || transport->poll == NULL || transport->cancel == NULL
        || !dht20_interval_valid(config->measurement_interval_ms)
        || !dht20_interval_valid(config->transfer_timeout_ms)
        || !dht20_interval_valid(config->conversion_timeout_ms)
        || config->conversion_timeout_ms <= DHT20_CONVERSION_MS
        || !dht20_interval_valid(config->retry_delay_ms)
        || !dht20_interval_valid(config->stale_after_ms)) {
        return APP_INVALID_ARGUMENT;
    }
    if (dht20_driver.active || (dht20_driver.state != DHT20_UNINITIALIZED
        && dht20_driver.state != DHT20_UNAVAILABLE)) {
        return APP_BUSY;
    }
    /* Retained timestamps require the same monotonic clock domain. */
    if (dht20_driver.has_sample && (clock_ms != dht20_driver.clock_ms
        || clock_context != dht20_driver.clock_context)) {
        return APP_INVALID_ARGUMENT;
    }
    if (dht20_driver.configured) {
        /* Account using the OLD clock and threshold before changing settings. */
        dht20_update_age(dht20_driver.clock_ms(dht20_driver.clock_context));
    }
    dht20_driver.bus = *transport;
    dht20_driver.config = *config;
    dht20_driver.clock_ms = clock_ms;
    dht20_driver.clock_context = clock_context;
    dht20_driver.configured = true;
    if (!dht20_driver.has_sample) {
        dht20_driver.age_observed_ms = clock_ms(clock_context);
    }
    dht20_driver.forced_stale = true;
    dht20_driver.frame_pending = false;
    return APP_OK;
}

app_status_t dht20_init(void)
{
    uint32_t now;
    if (!dht20_driver.configured) {
        return APP_NOT_READY;
    }
    now = dht20_driver.clock_ms(dht20_driver.clock_context);
    dht20_update_age(now);
    dht20_driver.forced_stale = true;
    dht20_driver.exhausted = false;
    dht20_driver.retries = 0u;
    dht20_driver.error = DHT20_ERROR_NONE;
    dht20_driver.stopping = false;
    if (dht20_driver.active) {
        dht20_driver.restart = true;
        if (dht20_driver.state != DHT20_CANCEL_WAIT) {
            dht20_driver.state = DHT20_CANCEL_REQUEST;
        }
    } else {
        dht20_power_up(now);
    }
    return APP_OK;
}

app_status_t dht20_stop(void)
{
    if (!dht20_driver.configured) {
        return APP_NOT_READY;
    }
    dht20_driver.stopping = true;
    dht20_driver.restart = false;
    dht20_driver.forced_stale = true;
    if (dht20_driver.active) {
        if (dht20_driver.state != DHT20_CANCEL_WAIT) {
            dht20_driver.state = DHT20_CANCEL_REQUEST;
        }
    } else {
        dht20_driver.state = DHT20_UNINITIALIZED;
        dht20_driver.frame_pending = false;
    }
    return APP_OK;
}

/* At most one backend call. request_ms bounds pre-acceptance bus contention. */
static void dht20_submit(bool receive, size_t length,
                          dht20_state_t next, uint32_t now)
{
    i2c_async_result_t result;
    if (dht20_elapsed(now, dht20_driver.request_ms,
                       dht20_driver.config.transfer_timeout_ms)) {
        dht20_fail(DHT20_ERROR_BUS_BUSY, now);
        return;
    }
    if (receive) {
        result = dht20_driver.bus.receive(dht20_driver.bus.context,
            &dht20_driver, DHT20_ADDRESS_7BIT, dht20_driver.rx, length,
            &dht20_driver.token);
    } else {
        result = dht20_driver.bus.transmit(dht20_driver.bus.context,
            &dht20_driver, DHT20_ADDRESS_7BIT, dht20_driver.tx, length,
            &dht20_driver.token);
    }
    if (result == I2C_ASYNC_ACCEPTED) {
        dht20_driver.active = true;
        dht20_driver.after_transfer = next;
        dht20_driver.state = DHT20_TRANSFER_WAIT;
        dht20_driver.since_ms = now;
    } else if (result != I2C_ASYNC_BUSY) {
        dht20_fail(result == I2C_ASYNC_ERROR ? DHT20_ERROR_TRANSFER
                                           : DHT20_ERROR_BACKEND, now);
    }
}

static void dht20_next_request(dht20_state_t state, uint32_t now)
{
    dht20_driver.state = state;
    dht20_driver.request_ms = now;
}

void dht20_task(void)
{
    uint32_t now;
    i2c_async_result_t result;
    if (!dht20_driver.configured) {
        return;
    }
    now = dht20_driver.clock_ms(dht20_driver.clock_context);
    dht20_update_age(now);
    switch (dht20_driver.state) {
    case DHT20_POWER_UP_WAIT:
        if (dht20_elapsed(now, dht20_driver.since_ms, DHT20_POWER_UP_MS)) {
            dht20_next_request(DHT20_STATUS_REQUEST, now);
        }
        break;
    case DHT20_STATUS_REQUEST:
        /* Demo's 0x71 is address + read, not a transmitted command. */
        dht20_submit(true, 1u, DHT20_STATUS_CHECK, now);
        break;
    case DHT20_STATUS_CHECK:
        if ((dht20_driver.rx[0] & 0x80u) != 0u) {
            dht20_fail(DHT20_ERROR_SENSOR_BUSY, now);
        } else if ((dht20_driver.rx[0] & 0x18u) == 0x18u) {
            dht20_driver.state = DHT20_IDLE;
            dht20_driver.since_ms = now;
        } else if (!dht20_driver.repaired) {
            dht20_driver.repaired = true;
            dht20_next_request(DHT20_CAL_WRITE_REQUEST, now);
        } else {
            dht20_fail((dht20_driver.rx[0] & 0x10u) == 0u
                ? DHT20_ERROR_OTP : DHT20_ERROR_CALIBRATION, now);
        }
        break;
    case DHT20_CAL_WRITE_REQUEST:
        dht20_driver.tx[0] = dht20_registers[dht20_driver.reg_index];
        dht20_driver.tx[1] = 0u;
        dht20_driver.tx[2] = 0u;
        dht20_submit(false, 3u, DHT20_CAL_READ_DELAY, now);
        break;
    case DHT20_CAL_READ_DELAY:
        if (dht20_elapsed(now, dht20_driver.since_ms, 5u)) {
            dht20_next_request(DHT20_CAL_READ_REQUEST, now);
        }
        break;
    case DHT20_CAL_READ_REQUEST:
        dht20_submit(true, 3u, DHT20_CAL_RESTORE_DELAY, now);
        break;
    case DHT20_CAL_RESTORE_DELAY:
        if (dht20_elapsed(now, dht20_driver.since_ms, 10u)) {
            dht20_next_request(DHT20_CAL_RESTORE_REQUEST, now);
        }
        break;
    case DHT20_CAL_RESTORE_REQUEST:
        dht20_driver.tx[0] = (uint8_t)(0xB0u | dht20_registers[dht20_driver.reg_index]);
        dht20_driver.tx[1] = dht20_driver.rx[1];
        dht20_driver.tx[2] = dht20_driver.rx[2];
        dht20_submit(false, 3u, DHT20_CAL_NEXT, now);
        break;
    case DHT20_CAL_NEXT:
        ++dht20_driver.reg_index;
        if (dht20_driver.reg_index < 3u) {
            dht20_next_request(DHT20_CAL_WRITE_REQUEST, now);
        } else {
            dht20_driver.state = DHT20_CAL_FINISH_WAIT;
            dht20_driver.since_ms = now;
        }
        break;
    case DHT20_CAL_FINISH_WAIT:
        if (dht20_elapsed(now, dht20_driver.since_ms, 10u)) {
            dht20_next_request(DHT20_STATUS_REQUEST, now);
        }
        break;
    case DHT20_IDLE:
        if (dht20_elapsed(now, dht20_driver.since_ms, 10u)
            && (!dht20_driver.has_sample || dht20_driver.forced_stale
                || dht20_elapsed(now, dht20_driver.last_success_ms,
                    dht20_driver.config.measurement_interval_ms))) {
            dht20_next_request(DHT20_MEASUREMENT_REQUEST, now);
        }
        break;
    case DHT20_MEASUREMENT_REQUEST:
        dht20_driver.tx[0] = 0xACu;
        dht20_driver.tx[1] = 0x33u;
        dht20_driver.tx[2] = 0x00u;
        dht20_submit(false, 3u, DHT20_CONVERSION_WAIT, now);
        break;
    case DHT20_CONVERSION_WAIT:
        if (dht20_elapsed(now, dht20_driver.since_ms, DHT20_CONVERSION_MS)) {
            dht20_driver.conversion_ms = dht20_driver.since_ms;
            dht20_next_request(DHT20_READ_REQUEST, now);
        }
        break;
    case DHT20_READ_REQUEST:
        if (dht20_elapsed(now, dht20_driver.conversion_ms,
                          dht20_driver.config.conversion_timeout_ms)) {
            dht20_fail(DHT20_ERROR_SENSOR_BUSY, now);
        } else {
            dht20_submit(true, 7u, DHT20_VALIDATE, now);
        }
        break;
    case DHT20_VALIDATE: {
        dht20_sample_t sample;
        const app_status_t parsed = dht20_protocol_parse_frame(dht20_driver.rx,
            sizeof dht20_driver.rx, &sample);
        if (parsed == APP_OK) {
            dht20_driver.sample = sample;
            dht20_driver.has_sample = true;
            /* Publish protocol-valid frames even when already stale. */
            dht20_driver.forced_stale = false;
            dht20_driver.age_stale = dht20_driver.frame_age_ms
                >= dht20_driver.config.stale_after_ms;
            dht20_driver.last_success_ms = dht20_driver.frame_received_ms;
            dht20_driver.age_ms = dht20_driver.frame_age_ms;
            dht20_driver.frame_pending = false;
            dht20_driver.error = DHT20_ERROR_NONE;
            dht20_driver.retries = 0u;
            dht20_driver.state = DHT20_IDLE;
            dht20_driver.since_ms = now;
        } else if (parsed == APP_BUSY) {
            dht20_driver.frame_pending = false;
            dht20_next_request(DHT20_READ_REQUEST, now);
        } else if (parsed == APP_NOT_READY) {
            dht20_fail(DHT20_ERROR_CALIBRATION, now);
        } else {
            uint8_t crc = 0u;
            (void)dht20_protocol_crc8(dht20_driver.rx, 6u, &crc);
            dht20_fail(crc != dht20_driver.rx[6] ? DHT20_ERROR_CRC
                                                : DHT20_ERROR_OTP, now);
        }
        break;
    }
    case DHT20_TRANSFER_WAIT:
        if (dht20_elapsed(now, dht20_driver.since_ms,
                          dht20_driver.config.transfer_timeout_ms)) {
            dht20_fail(DHT20_ERROR_TIMEOUT, now);
            break;
        }
        result = dht20_driver.bus.poll(dht20_driver.bus.context,
            &dht20_driver, dht20_driver.token);
        if (result == I2C_ASYNC_COMPLETE) {
            /* Timestamp observation after the bounded backend call returns. */
            now = dht20_driver.clock_ms(dht20_driver.clock_context);
            dht20_update_age(now);
            dht20_driver.active = false;
            if (dht20_driver.after_transfer == DHT20_VALIDATE) {
                dht20_driver.frame_received_ms = now;
                dht20_driver.frame_age_ms = 0u;
                dht20_driver.frame_pending = true;
            }
            dht20_driver.state = dht20_driver.after_transfer;
            dht20_driver.since_ms = now;
        } else if (result == I2C_ASYNC_ERROR) {
            dht20_driver.active = false;
            dht20_fail(DHT20_ERROR_TRANSFER, now);
        } else if (result != I2C_ASYNC_PENDING) {
            dht20_fail(DHT20_ERROR_BACKEND, now);
        }
        break;
    case DHT20_CANCEL_REQUEST:
    case DHT20_CANCEL_WAIT:
        result = dht20_driver.state == DHT20_CANCEL_REQUEST
            ? dht20_driver.bus.cancel(dht20_driver.bus.context,
                &dht20_driver, dht20_driver.token)
            : dht20_driver.bus.poll(dht20_driver.bus.context,
                &dht20_driver, dht20_driver.token);
        dht20_driver.state = DHT20_CANCEL_WAIT;
        if (result == I2C_ASYNC_COMPLETE || result == I2C_ASYNC_ERROR) {
            dht20_driver.active = false;
            if (dht20_driver.stopping) {
                dht20_driver.state = DHT20_UNINITIALIZED;
            } else if (dht20_driver.restart) {
                dht20_power_up(now);
            } else {
                dht20_fail(dht20_driver.error, now);
            }
        }
        break;
    case DHT20_ERROR_RECOVERY:
        if (dht20_elapsed(now, dht20_driver.since_ms,
                          dht20_driver.config.retry_delay_ms)) {
            dht20_power_up(now);
        }
        break;
    case DHT20_UNINITIALIZED:
    case DHT20_UNAVAILABLE:
    default:
        break;
    }
}

app_status_t dht20_get_info(dht20_info_t *info)
{
    if (info == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (!dht20_driver.configured) {
        return APP_NOT_READY;
    }
    dht20_update_age(dht20_driver.clock_ms(dht20_driver.clock_context));
    info->state = dht20_driver.state;
    info->last_error = dht20_driver.error;
    info->has_sample = dht20_driver.has_sample;
    info->last_success_ms = dht20_driver.last_success_ms;
    info->sample_age_ms = dht20_driver.age_ms;
    info->retries_used = dht20_driver.retries;
    info->retry_exhausted = dht20_driver.exhausted;
    if (dht20_driver.state == DHT20_UNAVAILABLE
        || dht20_driver.state == DHT20_UNINITIALIZED
        || dht20_driver.state == DHT20_CANCEL_REQUEST
        || dht20_driver.state == DHT20_CANCEL_WAIT) {
        info->sample_status = DHT20_SAMPLE_UNAVAILABLE;
    } else if (!dht20_driver.has_sample) {
        info->sample_status = DHT20_SAMPLE_NEVER;
    } else if (dht20_driver.forced_stale || dht20_driver.age_stale
        || dht20_driver.age_ms >= dht20_driver.config.stale_after_ms) {
        info->sample_status = DHT20_SAMPLE_STALE;
    } else {
        info->sample_status = DHT20_SAMPLE_FRESH;
    }
    return APP_OK;
}

app_status_t dht20_get_sample(dht20_sample_t *sample)
{
    dht20_info_t info;
    app_status_t status;
    if (sample == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    status = dht20_get_info(&info);
    if (status != APP_OK) {
        return status;
    }
    if (info.has_sample) {
        *sample = dht20_driver.sample;
    }
    if (info.sample_status == DHT20_SAMPLE_UNAVAILABLE) {
        return APP_IO_ERROR;
    }
    return info.sample_status == DHT20_SAMPLE_FRESH ? APP_OK : APP_NOT_READY;
}
