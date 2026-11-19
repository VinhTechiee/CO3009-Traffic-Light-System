#include "fsm_automatic.h"

#include "software_timer.h"
#include "traffic_light.h"

#include <stddef.h>
#include <stdint.h>

/* The course project uses a 10 ms TIM2/Scheduler tick. The Software Timer
 * API accepts ticks, while timing_config_t stores milliseconds. */
#ifndef FSM_AUTOMATIC_TICK_MS
#define FSM_AUTOMATIC_TICK_MS 10U
#endif

#if (FSM_AUTOMATIC_TICK_MS == 0U)
#error "FSM_AUTOMATIC_TICK_MS must be greater than zero"
#endif

typedef enum
{
    FSM_AUTO_PHASE_RED1_GREEN2 = 0,
    FSM_AUTO_PHASE_RED1_YELLOW2,
    FSM_AUTO_PHASE_GREEN1_RED2,
    FSM_AUTO_PHASE_YELLOW1_RED2
} fsm_auto_phase_t;

static timing_config_t g_timing;
static software_timer_id_t g_phase_timer_id;
static fsm_auto_phase_t g_phase;
static uint8_t g_active;
static uint8_t g_config_valid;

static app_status_t apply_pair(traffic_color_t road1, traffic_color_t road2)
{
    traffic_state_t state;

    state.road_1 = road1;
    state.road_2 = road2;
    return traffic_light_apply(&state);
}

static void enter_safe_all_red(void)
{
    (void)apply_pair(TRAFFIC_RED, TRAFFIC_RED);
}

static uint8_t milliseconds_to_ticks(uint32_t milliseconds,
                                     uint32_t *ticks_out)
{
    uint64_t ticks;

    if ((milliseconds == 0U) || (ticks_out == NULL))
    {
        return 0U;
    }

    /* Round upward so the requested duration is never shortened by the
     * 10 ms Scheduler resolution. */
    ticks = ((uint64_t)milliseconds + (FSM_AUTOMATIC_TICK_MS - 1U)) /
            FSM_AUTOMATIC_TICK_MS;

    if ((ticks == 0U) || (ticks > UINT32_MAX))
    {
        return 0U;
    }

    *ticks_out = (uint32_t)ticks;
    return 1U;
}

static app_status_t start_phase(fsm_auto_phase_t phase)
{
    uint32_t duration_ticks;
    software_timer_id_t new_timer_id = 0U;
    app_status_t status;
    traffic_color_t road1;
    traffic_color_t road2;

    switch (phase)
    {
        case FSM_AUTO_PHASE_RED1_GREEN2:
            duration_ticks = (uint32_t)(((uint64_t)g_timing.green_ms +
                                         FSM_AUTOMATIC_TICK_MS - 1U) /
                                        FSM_AUTOMATIC_TICK_MS);
            road1 = TRAFFIC_RED;
            road2 = TRAFFIC_GREEN;
            break;

        case FSM_AUTO_PHASE_RED1_YELLOW2:
            duration_ticks = (uint32_t)(((uint64_t)g_timing.yellow_ms +
                                         FSM_AUTOMATIC_TICK_MS - 1U) /
                                        FSM_AUTOMATIC_TICK_MS);
            road1 = TRAFFIC_RED;
            road2 = TRAFFIC_YELLOW;
            break;

        case FSM_AUTO_PHASE_GREEN1_RED2:
            duration_ticks = (uint32_t)(((uint64_t)g_timing.green_ms +
                                         FSM_AUTOMATIC_TICK_MS - 1U) /
                                        FSM_AUTOMATIC_TICK_MS);
            road1 = TRAFFIC_GREEN;
            road2 = TRAFFIC_RED;
            break;

        case FSM_AUTO_PHASE_YELLOW1_RED2:
            duration_ticks = (uint32_t)(((uint64_t)g_timing.yellow_ms +
                                         FSM_AUTOMATIC_TICK_MS - 1U) /
                                        FSM_AUTOMATIC_TICK_MS);
            road1 = TRAFFIC_YELLOW;
            road2 = TRAFFIC_RED;
            break;

        default:
            return APP_INVALID_ARGUMENT;
    }

    if (duration_ticks == 0U)
    {
        return APP_INVALID_ARGUMENT;
    }

    status = software_timer_start(duration_ticks, &new_timer_id);
    if (status != APP_OK)
    {
        return status;
    }

    status = apply_pair(road1, road2);
    if (status != APP_OK)
    {
        (void)software_timer_cancel(new_timer_id);
        return status;
    }

    g_phase = phase;
    g_phase_timer_id = new_timer_id;
    return APP_OK;
}

static void stop_after_error(void)
{
    if (g_phase_timer_id != 0U)
    {
        (void)software_timer_cancel(g_phase_timer_id);
    }

    g_phase_timer_id = 0U;
    g_active = 0U;
    enter_safe_all_red();
}

app_status_t fsm_automatic_enter(const timing_config_t *config)
{
    uint32_t green_ticks;
    uint32_t yellow_ticks;
    uint64_t expected_red_ms;
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

    if ((milliseconds_to_ticks(config->green_ms, &green_ticks) == 0U) ||
        (milliseconds_to_ticks(config->yellow_ms, &yellow_ticks) == 0U))
    {
        return APP_INVALID_ARGUMENT;
    }

    (void)green_ticks;
    (void)yellow_ticks;

    /* The course guide's four alternating phases imply that each road's RED
     * interval is exactly the opposite road's GREEN + YELLOW interval. There
     * is no separate all-red phase in this four-state model. */
    expected_red_ms = (uint64_t)config->green_ms + (uint64_t)config->yellow_ms;
    if (expected_red_ms != (uint64_t)config->red_ms)
    {
        return APP_INVALID_ARGUMENT;
    }

    g_timing = *config; /* Copy config so caller may safely reuse its object. */
    g_config_valid = 1U;

    /* Establish a known safe output before allowing either road to go GREEN. */
    status = apply_pair(TRAFFIC_RED, TRAFFIC_RED);
    if (status != APP_OK)
    {
        g_config_valid = 0U;
        return status;
    }

    g_phase_timer_id = 0U;
    status = start_phase(FSM_AUTO_PHASE_RED1_GREEN2);
    if (status != APP_OK)
    {
        stop_after_error();
        return status;
    }

    g_active = 1U;
    return APP_OK;
}

void fsm_automatic_exit(void)
{
    if (g_phase_timer_id != 0U)
    {
        (void)software_timer_cancel(g_phase_timer_id);
    }

    g_phase_timer_id = 0U;
    g_active = 0U;
    enter_safe_all_red();
}

void fsm_automatic_on_button(const button_event_t *event)
{
    /* Physical button IDs and their actions are intentionally not assigned in
     * global.h. fsm_mode owns mode-switch events; do not invent button mapping
     * or let an arbitrary button event change traffic-light safety states. */
    (void)event;
}

void fsm_automatic_task(void)
{
    bool expired = false;
    app_status_t status;
    fsm_auto_phase_t next_phase;

    if ((g_active == 0U) || (g_config_valid == 0U) ||
        (g_phase_timer_id == 0U))
    {
        return;
    }

    status = software_timer_is_expired(g_phase_timer_id, &expired);
    if (status != APP_OK)
    {
        stop_after_error();
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
        stop_after_error();
        return;
    }

    switch (g_phase)
    {
        case FSM_AUTO_PHASE_RED1_GREEN2:
            next_phase = FSM_AUTO_PHASE_RED1_YELLOW2;
            break;

        case FSM_AUTO_PHASE_RED1_YELLOW2:
            next_phase = FSM_AUTO_PHASE_GREEN1_RED2;
            break;

        case FSM_AUTO_PHASE_GREEN1_RED2:
            next_phase = FSM_AUTO_PHASE_YELLOW1_RED2;
            break;

        case FSM_AUTO_PHASE_YELLOW1_RED2:
            next_phase = FSM_AUTO_PHASE_RED1_GREEN2;
            break;

        default:
            stop_after_error();
            return;
    }

    /* Every phase change passes through ALL-RED. This is conservative and
     * prevents a transient dual-GREEN while the driver changes both outputs. */
    status = apply_pair(TRAFFIC_RED, TRAFFIC_RED);
    if (status != APP_OK)
    {
        stop_after_error();
        return;
    }

    status = start_phase(next_phase);
    if (status != APP_OK)
    {
        stop_after_error();
    }
}
