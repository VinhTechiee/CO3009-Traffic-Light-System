#include "app_runtime.h"

#include <stddef.h>

#include "fsm_automatic.h"
#include "fsm_mode.h"
#include "lcd_display.h"
#include "scheduler.h"
#include "software_timer.h"
#include "traffic_light.h"

static bool app_runtime_started;

static const timing_config_t app_initial_timing = {
    .red_ms = 5000U, .yellow_ms = 2000U, .green_ms = 3000U
};

/* Default course demo settings until the detailed course handout is available. */
static const fsm_mode_config_t app_modes = {
    .timing = {
        .red = {2000U, 40000U},
        .green = {1000U, 30000U},
        .yellow = {1000U, 10000U},
        .step_ms = 1000U,
        .red_adjusts = TRAFFIC_GREEN
    },
    .after_confirm = APP_MODE_AUTOMATIC,
    .after_cancel = APP_MODE_AUTOMATIC,
    .automatic_phase = fsm_automatic_get_phase,
    .bindings = {
        [APP_MODE_AUTOMATIC] = {
            {.kind = FSM_MODE_SWITCH, .button = BUTTON_ID_1,
             .trigger = BUTTON_EVENT_PRESS, .mode = APP_MODE_MANUAL}
        },
        [APP_MODE_MANUAL] = {
            {.kind = FSM_MODE_SWITCH, .button = BUTTON_ID_1,
             .trigger = BUTTON_EVENT_PRESS, .mode = APP_MODE_TUNING}
        },
        [APP_MODE_TUNING] = {
            {.kind = FSM_MODE_TUNING_ACTION, .button = BUTTON_ID_1,
             .trigger = BUTTON_EVENT_PRESS, .action = FSM_TUNING_CANCEL},
            {.kind = FSM_MODE_TUNING_ACTION, .button = BUTTON_ID_2,
             .trigger = BUTTON_EVENT_SHORT, .action = FSM_TUNING_NEXT},
            {.kind = FSM_MODE_TUNING_ACTION, .button = BUTTON_ID_2,
             .trigger = BUTTON_EVENT_LONG, .action = FSM_TUNING_CONFIRM},
            {.kind = FSM_MODE_TUNING_ACTION, .button = BUTTON_ID_3,
             .trigger = BUTTON_EVENT_PRESS, .action = FSM_TUNING_INCREASE},
            {.kind = FSM_MODE_TUNING_ACTION, .button = BUTTON_ID_4,
             .trigger = BUTTON_EVENT_PRESS, .action = FSM_TUNING_DECREASE},
            {.kind = FSM_MODE_TUNING_ACTION, .button = BUTTON_ID_3,
             .trigger = BUTTON_EVENT_REPEAT, .action = FSM_TUNING_INCREASE},
            {.kind = FSM_MODE_TUNING_ACTION, .button = BUTTON_ID_4,
             .trigger = BUTTON_EVENT_REPEAT, .action = FSM_TUNING_DECREASE}
        }
    }
};

static app_status_t app_start_sensor(const app_runtime_config_t *config)
{
    const dht20_config_t sensor = {
        .measurement_interval_ms = 2000U,
        .transfer_timeout_ms = 100U,
        .conversion_timeout_ms = 250U,
        .retry_delay_ms = 2000U,
        .stale_after_ms = 5000U,
        .max_retries = 2U
    };
    if (config->bus == NULL) {
        return APP_NOT_READY;
    }
    const app_status_t status = dht20_configure(config->bus, config->clock_ms,
                                               config->clock_context, &sensor);
    return status == APP_OK ? dht20_init() : status;
}

static void app_retry_sensor(void)
{
    dht20_info_t info;
    if (dht20_get_info(&info) == APP_OK && info.state == DHT20_UNAVAILABLE
        && info.retry_exhausted) {
        (void)dht20_init();
    }
}

static app_status_t app_start_display(const lcd_config_t *config)
{
    if (config == NULL) {
        return APP_NOT_READY;
    }
    if (config->rows < 2U) {
        return APP_INVALID_ARGUMENT;
    }
    const lcd_display_layout_t layout = {
        .mode_row = 0U,
        .timing_row = 1U,
        .sensor_row = config->rows >= 3U ? 2U : 1U
    };
    app_status_t status = lcd_configure(config);
    if (status == APP_OK) {
        status = lcd_init();
    }
    return status == APP_OK ? lcd_display_configure(&layout) : status;
}

app_status_t app_runtime_start(const app_runtime_config_t *config,
                               app_runtime_services_t *services)
{
    if (config == NULL || services == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (app_runtime_started) {
        return APP_BUSY;
    }
    *services = (app_runtime_services_t){APP_NOT_READY, APP_NOT_READY};

    app_status_t status = traffic_light_init();
    if (status != APP_OK) {
        return status;
    }
    if (config->buttons_enabled) {
        status = button_configure_active_low(config->button_active_low_mask);
        if (status == APP_OK) {
            status = button_init();
        }
        if (status != APP_OK) {
            return status;
        }
    }

    SCH_Init();
    status = software_timer_init_with_clock(SCH_Now);
    if (status == APP_OK) {
        status = fsm_mode_configure(&app_modes);
    }
    if (status != APP_OK) {
        return status;
    }

    services->sensor = app_start_sensor(config);
    services->display = app_start_display(config->lcd);
    const bool sensor_enabled = services->sensor == APP_OK;
    const bool display_enabled = services->display == APP_OK;
    const struct {
        sch_task_fn_t run;
        uint32_t period_ticks;
        bool enabled;
    } tasks[] = {
        {button_task,       1U, config->buttons_enabled},
        {fsm_mode_task,     1U, true},
        {dht20_task,        1U, sensor_enabled},
        {lcd_task,          1U, display_enabled},
        {lcd_display_task, 10U, display_enabled},
        {app_retry_sensor, 500U, sensor_enabled}
    };

    sch_task_id_t ids[sizeof tasks / sizeof tasks[0]];
    size_t registered = 0U;
    for (size_t i = 0U; i < sizeof tasks / sizeof tasks[0]; ++i) {
        if (tasks[i].enabled) {
            status = SCH_Add_Task(tasks[i].run, tasks[i].period_ticks, tasks[i].period_ticks,
                                  &ids[registered]);
            if (status != APP_OK) {
                break;
            }
            ++registered;
        }
    }
    if (status == APP_OK) {
        status = fsm_mode_init(APP_MODE_AUTOMATIC, &app_initial_timing);
    }
    if (status != APP_OK) {
        while (registered > 0U) {
            (void)SCH_Delete_Task(ids[--registered]);
        }
    } else {
        app_runtime_started = true;
    }
    return status;
}
