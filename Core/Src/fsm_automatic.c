#include "fsm_automatic.h"

#include "software_timer.h"
#include "timing_rules.h"
#include "traffic_light.h"

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    AUTO_PHASE_ROAD1_RED_ROAD2_GREEN = 0,
    AUTO_PHASE_ROAD1_RED_ROAD2_YELLOW,
    AUTO_PHASE_BOTH_RED_CLEARANCE,
    AUTO_PHASE_ROAD1_GREEN_ROAD2_RED,
    AUTO_PHASE_ROAD1_YELLOW_ROAD2_RED
} automatic_phase_t;

static uint8_t g_active;
static automatic_phase_t g_phase;
static traffic_state_t g_applied_phase;
static uint32_t g_green_ticks;
static uint32_t g_yellow_ticks;
static uint32_t g_clearance_ticks;
static software_timer_id_t g_phase_timer_id;

static app_status_t apply_traffic_pair(traffic_color_t road1,
                                       traffic_color_t road2)
{
    const traffic_state_t state = {road1, road2};
    const app_status_t status = traffic_light_apply(&state);
    if (status == APP_OK)
    {
        g_applied_phase = state;
    }
    return status;
}

static app_status_t apply_all_red(void)
{
    return apply_traffic_pair(TRAFFIC_RED, TRAFFIC_RED);
}

/* Start one phase. Timer is allocated first; if the output driver rejects
 * the requested pair, the new timer is cancelled. traffic_light_apply() is
 * responsible for safe physical write ordering during each pair update. */
static app_status_t start_phase(automatic_phase_t phase)
{
    uint32_t duration_ticks;
    traffic_color_t road1;
    traffic_color_t road2;
    software_timer_id_t new_timer_id = 0U;
    app_status_t status;

    switch (phase)
    {
        case AUTO_PHASE_ROAD1_RED_ROAD2_GREEN:
            duration_ticks = g_green_ticks;
            road1 = TRAFFIC_RED;
            road2 = TRAFFIC_GREEN;
            break;

        case AUTO_PHASE_ROAD1_RED_ROAD2_YELLOW:
            duration_ticks = g_yellow_ticks;
            road1 = TRAFFIC_RED;
            road2 = TRAFFIC_YELLOW;
            break;

        case AUTO_PHASE_BOTH_RED_CLEARANCE:
            duration_ticks = g_clearance_ticks;
            road1 = TRAFFIC_RED;
            road2 = TRAFFIC_RED;
            break;

        case AUTO_PHASE_ROAD1_GREEN_ROAD2_RED:
            duration_ticks = g_green_ticks;
            road1 = TRAFFIC_GREEN;
            road2 = TRAFFIC_RED;
            break;

        case AUTO_PHASE_ROAD1_YELLOW_ROAD2_RED:
            duration_ticks = g_yellow_ticks;
            road1 = TRAFFIC_YELLOW;
            road2 = TRAFFIC_RED;
            break;

        default:
            return APP_INVALID_ARGUMENT;
    }

    /* A zero clearance is valid, but the transition logic skips that phase. */
    if (duration_ticks == 0U)
    {
        return APP_INVALID_ARGUMENT;
    }

    status = software_timer_start(duration_ticks, &new_timer_id);
    if (status != APP_OK)
    {
        return status;
    }

    status = apply_traffic_pair(road1, road2);
    if (status != APP_OK)
    {
        (void)software_timer_cancel(new_timer_id);
        return status;
    }

    g_phase = phase;
    g_phase_timer_id = new_timer_id;
    return APP_OK;
}

static void stop_safely(void)
{
    if (g_phase_timer_id != 0U)
    {
        (void)software_timer_cancel(g_phase_timer_id);
    }

    g_phase_timer_id = 0U;
    g_active = 0U;
    (void)apply_all_red();
}

/* The group's timing_config_t contains independent RED, YELLOW and GREEN
 * values in milliseconds. For this alternating cycle, RED must be at least
 * GREEN + YELLOW. The difference is implemented as one explicit BOTH-RED
 * clearance phase per complete cycle:
 *
 *   R1 RED / R2 GREEN  : GREEN
 *   R1 RED / R2 YELLOW : YELLOW
 *   BOTH RED           : RED - GREEN - YELLOW
 *   R1 GREEN / R2 RED  : GREEN
 *   R1 YELLOW / R2 RED : YELLOW
 *
 * Each road's RED interval is therefore GREEN + YELLOW + clearance. If the
 * clearance is zero, the BOTH-RED phase is skipped. Durations are rounded up
 * to 10 ms ticks, and validation also checks the quantized tick relationship. */
app_status_t fsm_automatic_enter(const timing_config_t *config)
{
    app_status_t status;

    if (config == NULL)
    {
        return APP_INVALID_ARGUMENT;
    }
    if (g_active != 0U)
    {
        return APP_BUSY;
    }
    if (!timing_config_valid(config))
    {
        return APP_INVALID_ARGUMENT;
    }

    /* Establish safe outputs before committing this configuration or starting
     * any GREEN phase. A driver failure leaves the FSM inactive. */
    status = apply_all_red();
    if (status != APP_OK)
    {
        return status;
    }

    g_green_ticks = timing_ticks_ceil(config->green_ms);
    g_yellow_ticks = timing_ticks_ceil(config->yellow_ms);
    g_clearance_ticks = timing_ticks_ceil(config->red_ms) - g_green_ticks - g_yellow_ticks;

    status = start_phase(AUTO_PHASE_ROAD1_RED_ROAD2_GREEN);
    if (status != APP_OK)
    {
        stop_safely();
        return status;
    }

    g_active = 1U;
    return APP_OK;
}

/* Called by fsm_mode during Auto -> Manual/Tuning handover. Scheduler task
 * registration is owned by the application/mode layer; this function stops
 * the Auto phase timer and requests both roads RED before handover. */
void fsm_automatic_exit(void)
{
    stop_safely();
}

app_status_t fsm_automatic_get_phase(traffic_state_t *phase)
{
    if (phase == NULL)
    {
        return APP_INVALID_ARGUMENT;
    }
    if (g_active == 0U)
    {
        return APP_NOT_READY;
    }
    *phase = g_applied_phase;
    return APP_OK;
}

static uint32_t automatic_seconds_ceil(uint32_t ticks)
{
    const uint64_t milliseconds = (uint64_t)ticks * FSM_AUTOMATIC_TICK_MS;
    return (uint32_t)((milliseconds + 999U) / 1000U);
}

app_status_t fsm_automatic_get_view(fsm_automatic_view_t *view)
{
    if (view == NULL)
    {
        return APP_INVALID_ARGUMENT;
    }
    if (g_active == 0U)
    {
        return APP_NOT_READY;
    }

    uint32_t phase_ticks;
    const app_status_t status = software_timer_get_remaining(g_phase_timer_id, &phase_ticks);
    if (status != APP_OK)
    {
        return status;
    }

    uint32_t road1_ticks = phase_ticks;
    uint32_t road2_ticks = phase_ticks;
    /* A red light can stay on across several phases. */
    switch (g_phase)
    {
        case AUTO_PHASE_ROAD1_RED_ROAD2_GREEN:
            road1_ticks += g_yellow_ticks + g_clearance_ticks;
            break;
        case AUTO_PHASE_ROAD1_RED_ROAD2_YELLOW:
            road1_ticks += g_clearance_ticks;
            break;
        case AUTO_PHASE_BOTH_RED_CLEARANCE:
            road2_ticks += g_green_ticks + g_yellow_ticks;
            break;
        case AUTO_PHASE_ROAD1_GREEN_ROAD2_RED:
            road2_ticks += g_yellow_ticks;
            break;
        case AUTO_PHASE_ROAD1_YELLOW_ROAD2_RED:
            break;
    }

    *view = (fsm_automatic_view_t){
        .colors = g_applied_phase,
        .road1_seconds = automatic_seconds_ceil(road1_ticks),
        .road2_seconds = automatic_seconds_ceil(road2_ticks)
    };
    return APP_OK;
}

/* Button IDs/actions are intentionally not mapped here. global.h defines
 * logical event types only; fsm_mode owns mode-switch policy and the Button
 * module owns physical button mapping/debouncing. */
void fsm_automatic_on_button(const button_event_t *event)
{
    (void)event;
}

void fsm_automatic_task(void)
{
    bool expired = false;
    app_status_t status;
    automatic_phase_t next_phase;

    if ((g_active == 0U) || (g_phase_timer_id == 0U))
    {
        return;
    }

    status = software_timer_is_expired(g_phase_timer_id, &expired);
    if (status != APP_OK)
    {
        stop_safely();
        return;
    }
    if (expired == false)
    {
        return;
    }

    status = software_timer_cancel(g_phase_timer_id);
    g_phase_timer_id = 0U;
    if (status != APP_OK)
    {
        stop_safely();
        return;
    }

    switch (g_phase)
    {
        case AUTO_PHASE_ROAD1_RED_ROAD2_GREEN:
            next_phase = AUTO_PHASE_ROAD1_RED_ROAD2_YELLOW;
            break;

        case AUTO_PHASE_ROAD1_RED_ROAD2_YELLOW:
            next_phase = (g_clearance_ticks > 0U)
                       ? AUTO_PHASE_BOTH_RED_CLEARANCE
                       : AUTO_PHASE_ROAD1_GREEN_ROAD2_RED;
            break;

        case AUTO_PHASE_BOTH_RED_CLEARANCE:
            next_phase = AUTO_PHASE_ROAD1_GREEN_ROAD2_RED;
            break;

        case AUTO_PHASE_ROAD1_GREEN_ROAD2_RED:
            next_phase = AUTO_PHASE_ROAD1_YELLOW_ROAD2_RED;
            break;

        case AUTO_PHASE_ROAD1_YELLOW_ROAD2_RED:
            next_phase = AUTO_PHASE_ROAD1_RED_ROAD2_GREEN;
            break;

        default:
            stop_safely();
            return;
    }

    status = start_phase(next_phase);
    if (status != APP_OK)
    {
        stop_safely();
    }
}
