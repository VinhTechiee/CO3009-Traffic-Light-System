#include "fsm_automatic.h"
#include "software_timer.h"
#include "traffic_light.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static traffic_state_t applied;
static uint32_t writes;
static bool reject_next_write;
static uint64_t clock_tick;

app_status_t traffic_light_apply(const traffic_state_t *state)
{
    assert(state != NULL);
    assert(!(state->road_1 == TRAFFIC_GREEN && state->road_2 == TRAFFIC_GREEN));
    ++writes;
    if (reject_next_write) {
        reject_next_write = false;
        return APP_IO_ERROR;
    }
    applied = *state;
    return APP_OK;
}

static void expect_pair(traffic_color_t road_1, traffic_color_t road_2)
{
    assert(applied.road_1 == road_1 && applied.road_2 == road_2);
}

static void expect_active_phase(traffic_color_t road_1, traffic_color_t road_2)
{
    traffic_state_t snapshot = {TRAFFIC_RED, TRAFFIC_RED};
    assert(fsm_automatic_get_phase(&snapshot) == APP_OK);
    assert(snapshot.road_1 == road_1 && snapshot.road_2 == road_2);
    expect_pair(road_1, road_2);
}

static void tick_once(void)
{
    software_timer_tick();
    fsm_automatic_task();
}

static void expect_duration(traffic_color_t road_1, traffic_color_t road_2,
                            uint32_t ticks)
{
    expect_active_phase(road_1, road_2);
    const uint32_t before = writes;
    fsm_automatic_task();
    for (uint32_t i = 1U; i < ticks; ++i) {
        tick_once();
        expect_active_phase(road_1, road_2);
    }
    assert(writes == before);
    tick_once();
    assert(writes == before + 1U);
}

static void test_duration_and_cycles(void)
{
    /* Rounded to 8 red, 2 yellow, 4 green and 2 all-red clearance ticks. */
    timing_config_t timing = {.red_ms = 77U, .yellow_ms = 11U, .green_ms = 35U};
    traffic_state_t inactive_snapshot = {TRAFFIC_YELLOW, TRAFFIC_YELLOW};
    assert(fsm_automatic_get_phase(NULL) == APP_INVALID_ARGUMENT);
    assert(fsm_automatic_get_phase(&inactive_snapshot) == APP_NOT_READY);
    assert(inactive_snapshot.road_1 == TRAFFIC_YELLOW);
    assert(fsm_automatic_enter(NULL) == APP_INVALID_ARGUMENT);

    software_timer_init();
    assert(fsm_automatic_enter(&timing) == APP_OK);
    assert(writes == 2U); /* Startup gates green on a successful all-red write. */
    assert(fsm_automatic_enter(&timing) == APP_BUSY);
    timing = (timing_config_t){0};
    for (uint32_t cycle = 0U; cycle < 5U; ++cycle) {
        expect_duration(TRAFFIC_RED, TRAFFIC_GREEN, 4U);
        expect_duration(TRAFFIC_RED, TRAFFIC_YELLOW, 2U);
        expect_duration(TRAFFIC_RED, TRAFFIC_RED, 2U);
        expect_duration(TRAFFIC_GREEN, TRAFFIC_RED, 4U);
        expect_duration(TRAFFIC_YELLOW, TRAFFIC_RED, 2U);
    }
    fsm_automatic_exit();
    assert(fsm_automatic_get_phase(&inactive_snapshot) == APP_NOT_READY);
    const uint32_t after_exit = writes;
    for (uint32_t i = 0U; i < 10U; ++i) {
        tick_once();
    }
    assert(writes == after_exit);
    expect_pair(TRAFFIC_RED, TRAFFIC_RED);

    const timing_config_t tuned = {.red_ms = 30U, .yellow_ms = 10U, .green_ms = 20U};
    assert(fsm_automatic_enter(&tuned) == APP_OK);
    expect_duration(TRAFFIC_RED, TRAFFIC_GREEN, 2U);
    expect_duration(TRAFFIC_RED, TRAFFIC_YELLOW, 1U);
    expect_duration(TRAFFIC_GREEN, TRAFFIC_RED, 2U);
    expect_duration(TRAFFIC_YELLOW, TRAFFIC_RED, 1U);
    expect_active_phase(TRAFFIC_RED, TRAFFIC_GREEN);
    fsm_automatic_exit();
}

static void test_validation_and_fault_cleanup(void)
{
    const timing_config_t invalid[] = {
        {.red_ms = 10U, .yellow_ms = 0U, .green_ms = 10U},
        {.red_ms = 19U, .yellow_ms = 10U, .green_ms = 10U},
        {.red_ms = 22U, .yellow_ms = 11U, .green_ms = 11U},
        {.red_ms = UINT32_MAX, .yellow_ms = UINT32_MAX, .green_ms = 1U}
    };
    const uint32_t before_invalid = writes;
    for (size_t i = 0U; i < sizeof invalid / sizeof invalid[0]; ++i) {
        assert(fsm_automatic_enter(&invalid[i]) == APP_INVALID_ARGUMENT);
    }
    assert(writes == before_invalid);

    const timing_config_t timing = {.red_ms = 20U, .yellow_ms = 10U, .green_ms = 10U};
    software_timer_init();
    software_timer_id_t timers[8];
    for (size_t i = 0U; i < sizeof timers / sizeof timers[0]; ++i) {
        assert(software_timer_start(100U, &timers[i]) == APP_OK);
    }
    assert(fsm_automatic_enter(&timing) == APP_NO_CAPACITY);
    expect_pair(TRAFFIC_RED, TRAFFIC_RED);
    assert(software_timer_cancel(timers[0]) == APP_OK);

    reject_next_write = true;
    assert(fsm_automatic_enter(&timing) == APP_IO_ERROR);
    assert(fsm_automatic_enter(&timing) == APP_OK);
    expect_active_phase(TRAFFIC_RED, TRAFFIC_GREEN);
    reject_next_write = true;
    tick_once();
    expect_pair(TRAFFIC_RED, TRAFFIC_RED);
    traffic_state_t snapshot;
    assert(fsm_automatic_get_phase(&snapshot) == APP_NOT_READY);

    /* A failed output transition frees only Automatic's newly allocated timer. */
    assert(software_timer_start(100U, &timers[0]) == APP_OK);
    for (size_t i = 0U; i < sizeof timers / sizeof timers[0]; ++i) {
        assert(software_timer_cancel(timers[i]) == APP_OK);
    }
    assert(fsm_automatic_enter(&timing) == APP_OK);
    software_timer_init();
    fsm_automatic_task();
    expect_pair(TRAFFIC_RED, TRAFFIC_RED);
    assert(fsm_automatic_get_phase(&snapshot) == APP_NOT_READY);
}

static uint64_t auto_clock(void) { return clock_tick; }

static void expect_countdown(traffic_color_t road1, uint32_t road1_seconds,
                             traffic_color_t road2, uint32_t road2_seconds)
{
    fsm_automatic_view_t view;
    assert(fsm_automatic_get_view(&view) == APP_OK);
    assert(view.colors.road_1 == road1 && view.road1_seconds == road1_seconds);
    assert(view.colors.road_2 == road2 && view.road2_seconds == road2_seconds);
}

static void test_countdown_across_phases(void)
{
    const timing_config_t timing = {.red_ms = 6000U, .yellow_ms = 2000U, .green_ms = 3000U};
    clock_tick = 0U;
    assert(software_timer_init_with_clock(auto_clock) == APP_OK);
    assert(fsm_automatic_enter(&timing) == APP_OK);
    expect_countdown(TRAFFIC_RED, 6U, TRAFFIC_GREEN, 3U);
    clock_tick = 101U;
    expect_countdown(TRAFFIC_RED, 5U, TRAFFIC_GREEN, 2U);
    clock_tick = 300U;
    fsm_automatic_task();
    expect_countdown(TRAFFIC_RED, 3U, TRAFFIC_YELLOW, 2U);
    clock_tick = 500U;
    fsm_automatic_task();
    expect_countdown(TRAFFIC_RED, 1U, TRAFFIC_RED, 6U);
    clock_tick = 600U;
    fsm_automatic_task();
    expect_countdown(TRAFFIC_GREEN, 3U, TRAFFIC_RED, 5U);
    clock_tick = 900U;
    fsm_automatic_task();
    expect_countdown(TRAFFIC_YELLOW, 2U, TRAFFIC_RED, 2U);
    fsm_automatic_exit();
}

static void test_delayed_dispatch(void)
{
    const timing_config_t timing = {.red_ms = 50U, .yellow_ms = 30U, .green_ms = 20U};
    assert(software_timer_init_with_clock(auto_clock) == APP_OK);
    assert(fsm_automatic_enter(&timing) == APP_OK);
    clock_tick = 1U;
    fsm_automatic_task();
    expect_active_phase(TRAFFIC_RED, TRAFFIC_GREEN);

    /* Late dispatch consumes one expired phase. Each newly applied phase
     * has its own full duration measured from that dispatch. */
    clock_tick = 21U;
    fsm_automatic_task();
    expect_active_phase(TRAFFIC_RED, TRAFFIC_YELLOW);
    clock_tick = 23U;
    fsm_automatic_task();
    expect_active_phase(TRAFFIC_RED, TRAFFIC_YELLOW);
    clock_tick = 24U;
    fsm_automatic_task();
    expect_active_phase(TRAFFIC_GREEN, TRAFFIC_RED);
    fsm_automatic_exit();
}

int main(void)
{
    test_duration_and_cycles();
    test_validation_and_fault_cleanup();
    test_delayed_dispatch();
    test_countdown_across_phases();
    puts("Automatic FSM tests passed");
    return 0;
}
