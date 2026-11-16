#ifndef CO3009_DHT20_H
#define CO3009_DHT20_H

#include "global.h"
#include "i2c_async.h"

typedef struct {
    float temperature_c;
    float relative_humidity_percent;
    bool valid;
} dht20_sample_t;
typedef uint32_t (*dht20_clock_ms_fn_t)(void *context);
typedef struct {
    uint32_t measurement_interval_ms;
    uint32_t transfer_timeout_ms;
    uint32_t conversion_timeout_ms; /* >80, separate from bus timeout */
    uint32_t retry_delay_ms;
    uint32_t stale_after_ms;
    uint8_t max_retries; /* additional attempts after first attempt */
} dht20_config_t;
typedef enum {
    DHT20_UNINITIALIZED, DHT20_POWER_UP_WAIT, DHT20_STATUS_REQUEST,
    DHT20_STATUS_CHECK, DHT20_CAL_WRITE_REQUEST, DHT20_CAL_READ_DELAY,
    DHT20_CAL_READ_REQUEST, DHT20_CAL_RESTORE_DELAY, DHT20_CAL_RESTORE_REQUEST,
    DHT20_CAL_NEXT, DHT20_CAL_FINISH_WAIT, DHT20_IDLE,
    DHT20_MEASUREMENT_REQUEST, DHT20_CONVERSION_WAIT, DHT20_READ_REQUEST,
    DHT20_VALIDATE, DHT20_TRANSFER_WAIT, DHT20_CANCEL_REQUEST,
    DHT20_CANCEL_WAIT, DHT20_ERROR_RECOVERY, DHT20_UNAVAILABLE
} dht20_state_t;
typedef enum {
    DHT20_ERROR_NONE, DHT20_ERROR_BUS_BUSY, DHT20_ERROR_TRANSFER,
    DHT20_ERROR_TIMEOUT, DHT20_ERROR_SENSOR_BUSY, DHT20_ERROR_CRC,
    DHT20_ERROR_CALIBRATION, DHT20_ERROR_OTP, DHT20_ERROR_BACKEND
} dht20_error_t;
typedef enum {
    DHT20_SAMPLE_NEVER, DHT20_SAMPLE_FRESH, DHT20_SAMPLE_STALE,
    DHT20_SAMPLE_UNAVAILABLE
} dht20_sample_status_t;
typedef struct {
    dht20_state_t state;
    dht20_error_t last_error; /* root cause retained after retry exhaustion */
    dht20_sample_status_t sample_status;
    bool has_sample;
    bool retry_exhausted;
    uint32_t last_success_ms; /* successful sample's observed frame reception, ms */
    uint32_t sample_age_ms; /* saturated at UINT32_MAX */
    uint16_t retries_used;
} dht20_info_t;

/* Foreground singleton preserving original APIs. Copies transport/config.
 * Clock/backend contexts must outlive use; NULL contexts permitted.
 * Rebind only uninitialized/unavailable with no active transfer.
 * Intervals: 1..INT32_MAX ms; conversion timeout >80 ms.
 * Invalid arguments leave driver unchanged. Clock observations <2^32 ms apart.
 * Reconfiguration preserves age/timestamp; retained samples require the same
 * clock function/context and epoch. Staleness latches until a new valid frame.
 */
app_status_t dht20_configure(const i2c_async_t *transport,
    dht20_clock_ms_fn_t clock_ms, void *clock_context,
    const dht20_config_t *config);
/* Starts/restarts asynchronous startup; APP_OK means accepted, not ready.
 * Cancels owned transfer before reuse. Retains last sample, forces stale.
 */
app_status_t dht20_init(void);
app_status_t dht20_stop(void); /* asynchronous stop/cancel, preserves sample */
/* Scheduled nonblocking measurement/transfer step. */
void dht20_task(void);
/* Copies last sample if any even when stale/unavailable; otherwise unchanged.
 * APP_OK=fresh; APP_NOT_READY=never/stale; APP_IO_ERROR=unavailable.
 * NULL -> APP_INVALID_ARGUMENT. See get_info for age/status/errors.
 * Delayed valid frames publish with their original reception time and may
 * immediately be stale. Invalid frames never replace the last-good sample.
 */
app_status_t dht20_get_sample(dht20_sample_t *sample);
app_status_t dht20_get_info(dht20_info_t *info);

#endif /* CO3009_DHT20_H */
