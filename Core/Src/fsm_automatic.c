#include "fsm_automatic.h"

#include "software_timer.h"
#include "traffic_light.h"

#include <stddef.h>
#include <stdint.h>

/* Course project TIM2/Scheduler tick: 10 ms. Override only if the group
 * changes the common tick and updates every module consistently. */
#ifndef FSM_AUTOMATIC_TICK_MS
#define FSM_AUTOMATIC_TICK_MS 10U
#endif

#if (FSM_AUTOMATIC_TICK_MS == 0U)
#error "FSM_AUTOMATIC_TICK_MS must be greater than zero"
#endif

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
static timing_config_t g_timing;
static uint32_t g_clearance_ticks;
static software_timer_id_t g_phase_timer_id;

static app_status_t apply_traffic_pair(traffic_color_t road1,
                                       traffic_color_t road2)
{
    traffic_state_t state;

    state.road_1 = road1;
    state.road_2 = road2;
    return traffic_light_apply(&state);
}

static app_status_t apply_all_red(void)
{
    app_status_t status = apply_traffic_pair(TRAFFIC_RED, TRAFFIC_RED);

    return status;
}

/* Convert milliseconds to scheduler ticks without shortening the requested
 * duration. The common scheduler tick is 10 ms in this project. */
static uint8_t milliseconds_to_ticks(uint32_t milliseconds,
                                     uint32_t *ticks_out)
{
    uint64_t ticks;

    if ((milliseconds == 0U) || (ticks_out == NULL))
    {
        return 0U;
    }

    ticks = ((uint64_t)milliseconds +
             ((uint64_t)FSM_AUTOMATIC_TICK_MS - 1U)) /
            (uint64_t)FSM_AUTOMATIC_TICK_MS;

    if ((ticks == 0U) || (ticks > UINT32_MAX))
    {
        return 0U;
    }

    *ticks_out = (uint32_t)ticks;
    return 1U;
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
            if (milliseconds_to_ticks(g_timing.green_ms, &duration_ticks) == 0U)
            {
                return APP_INVALID_ARGUMENT;
            }
            road1 = TRAFFIC_RED;
            road2 = TRAFFIC_GREEN;
            break;

        case AUTO_PHASE_ROAD1_RED_ROAD2_YELLOW:
            if (milliseconds_to_ticks(g_timing.yellow_ms, &duration_ticks) == 0U)
            {
                return APP_INVALID_ARGUMENT;
            }
            road1 = TRAFFIC_RED;
            road2 = TRAFFIC_YELLOW;
            break;

        case AUTO_PHASE_BOTH_RED_CLEARANCE:
            duration_ticks = g_clearance_ticks;
            road1 = TRAFFIC_RED;
            road2 = TRAFFIC_RED;
            break;

        case AUTO_PHASE_ROAD1_GREEN_ROAD2_RED:
            if (milliseconds_to_ticks(g_timing.green_ms, &duration_ticks) == 0U)
            {
                return APP_INVALID_ARGUMENT;
            }
            road1 = TRAFFIC_GREEN;
            road2 = TRAFFIC_RED;
            break;

        case AUTO_PHASE_ROAD1_YELLOW_ROAD2_RED:
            if (milliseconds_to_ticks(g_timing.yellow_ms, &duration_ticks) == 0U)
            {
                return APP_INVALID_ARGUMENT;
            }
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
    uint32_t red_ticks;
    uint32_t green_ticks;
    uint32_t yellow_ticks;
    uint64_t minimum_red_ms;
    uint64_t minimum_red_ticks;
    timing_config_t previous_timing;
    uint32_t previous_clearance_ticks;
    app_status_t status;

    if (config == NULL)
    {
        return APP_INVALID_ARGUMENT;
    }
    if (g_active != 0U)
    {
        return APP_BUSY;
    }
    if ((config->red_ms == 0U) ||
        (config->yellow_ms == 0U) ||
        (config->green_ms == 0U))
    {
        return APP_INVALID_ARGUMENT;
    }

    minimum_red_ms = (uint64_t)config->green_ms +
                     (uint64_t)config->yellow_ms;
    if ((uint64_t)config->red_ms < minimum_red_ms)
    {
        return APP_INVALID_ARGUMENT;
    }

    if ((milliseconds_to_ticks(config->red_ms, &red_ticks) == 0U) ||
        (milliseconds_to_ticks(config->green_ms, &green_ticks) == 0U) ||
        (milliseconds_to_ticks(config->yellow_ms, &yellow_ticks) == 0U))
    {
        return APP_INVALID_ARGUMENT;
    }

    minimum_red_ticks = (uint64_t)green_ticks + (uint64_t)yellow_ticks;
    if ((uint64_t)red_ticks < minimum_red_ticks)
    {
        /* Independent upward rounding can make the tick-level phases exceed
         * RED. Reject rather than silently produce a shorter RED interval. */
        return APP_INVALID_ARGUMENT;
    }

    /* Establish safe outputs before committing this configuration or starting
     * any GREEN phase. A driver failure leaves the FSM inactive. */
    status = apply_all_red();
    if (status != APP_OK)
    {
        return status;
    }

    previous_timing = g_timing;
    previous_clearance_ticks = g_clearance_ticks;
    g_timing = *config; /* Private copy required by the public header contract. */
    g_clearance_ticks = red_ticks - green_ticks - yellow_ticks;
    g_phase_timer_id = 0U;

    status = start_phase(AUTO_PHASE_ROAD1_RED_ROAD2_GREEN);
    if (status != APP_OK)
    {
        g_timing = previous_timing;
        g_clearance_ticks = previous_clearance_ticks;
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
    if (g_phase_timer_id != 0U)
    {
        (void)software_timer_cancel(g_phase_timer_id);
    }

    g_phase_timer_id = 0U;
    g_active = 0U;
    (void)apply_all_red();
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
