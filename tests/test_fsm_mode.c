#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "fsm_automatic.h"
#include "fsm_mode.h"
#include "fsm_manual.h"
#include "software_timer.h"
#include "traffic_light.h"

enum { ROAD_1 = (1U << 3) | (1U << 5), ROAD_2 = (1U << 4) | (1U << 10),
       ALL_RED = (1U << 5) | (1U << 10), P1 = (1U << 5) | ROAD_2,
       P2 = (1U << 5) | (1U << 4), P3 = ROAD_1 | (1U << 10) };

static uint16_t pins;
static uint32_t now_ms;
static bool high[BUTTON_ID_COUNT];
static bool snapshot_called;

static void write_gpio(uint16_t mask, bool set)
{
    pins = set ? (uint16_t)(pins | mask) : (uint16_t)(pins & ~mask);
    assert((pins & ROAD_1) != ROAD_1 || (pins & ROAD_2) != ROAD_2);
}

static bool read_level(button_id_t id)
{
    return high[id];
}

static uint32_t clock_ms(void)
{
    return now_ms;
}

static app_status_t snapshot(traffic_state_t *phase)
{
    if (snapshot_called) {
        return APP_NOT_READY;
    }
    assert(pins == P2);
    snapshot_called = true;
    *phase = (traffic_state_t){TRAFFIC_RED, TRAFFIC_YELLOW};
    return APP_OK;
}

static void sample(unsigned count, bool dispatch)
{
    for (unsigned i = 0; i < count; ++i) {
        now_ms += 10U;
        button_task();
        if (dispatch) {
            fsm_mode_task();
        }
    }
}

static void press(button_id_t id, unsigned hold_samples)
{
    high[id] = true;
    sample(hold_samples, true);
    high[id] = false;
    sample(5, true);
}

static void ticks(unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        software_timer_tick();
        fsm_mode_task();
    }
}

static void expect_mode(app_mode_t expected)
{
    app_mode_t actual;
    assert(fsm_mode_get(&actual) == APP_OK && actual == expected);
}

int main(void)
{
    fsm_mode_config_t cfg = {
        .timing = {{100, 500}, {50, 400}, {50, 400}, 10, TRAFFIC_GREEN},
        .after_confirm = APP_MODE_AUTOMATIC,
        .after_cancel = APP_MODE_MANUAL,
        .automatic_phase = snapshot,
        .bindings = {
            [APP_MODE_AUTOMATIC] = {
                {FSM_MODE_SWITCH, BUTTON_ID_1, BUTTON_EVENT_PRESS, APP_MODE_MANUAL, 0}
            },
            [APP_MODE_MANUAL] = {
                {FSM_MODE_SWITCH, BUTTON_ID_USER, BUTTON_EVENT_LONG, APP_MODE_TUNING, 0}
            },
            [APP_MODE_TUNING] = {
                {FSM_MODE_TUNING_ACTION, BUTTON_ID_3, BUTTON_EVENT_PRESS, 0, FSM_TUNING_INCREASE},
                {FSM_MODE_TUNING_ACTION, BUTTON_ID_USER, BUTTON_EVENT_SHORT, 0, FSM_TUNING_CONFIRM},
                {FSM_MODE_TUNING_ACTION, BUTTON_ID_USER, BUTTON_EVENT_LONG, 0, FSM_TUNING_CANCEL}
            }
        }
    };
    const timing_config_t initial = {150, 50, 100};
    const timing_config_t invalid = {152, 51, 101};
    app_mode_t mode;
    traffic_state_t phase;
    assert(fsm_mode_get(NULL) == APP_INVALID_ARGUMENT);
    assert(fsm_mode_get(&mode) == APP_NOT_READY);
    assert(fsm_mode_request(APP_MODE_AUTOMATIC) == APP_NOT_READY);
    assert(fsm_mode_init(APP_MODE_AUTOMATIC, &initial) == APP_NOT_READY);
    cfg.bindings[APP_MODE_TUNING][3] = (fsm_mode_binding_t){
        FSM_MODE_TUNING_ACTION, BUTTON_ID_USER, BUTTON_EVENT_PRESS, 0, FSM_TUNING_NEXT};
    assert(fsm_mode_configure(&cfg) == APP_INVALID_ARGUMENT);
    cfg.bindings[APP_MODE_TUNING][3] = (fsm_mode_binding_t){
        FSM_MODE_TUNING_ACTION, BUTTON_ID_3, BUTTON_EVENT_REPEAT, 0, FSM_TUNING_DECREASE};
    assert(fsm_mode_configure(&cfg) == APP_INVALID_ARGUMENT);
    cfg.bindings[APP_MODE_TUNING][3].action = FSM_TUNING_INCREASE;
    assert(fsm_mode_configure(&cfg) == APP_OK);
    assert(fsm_mode_init(APP_MODE_AUTOMATIC, &invalid) == APP_INVALID_ARGUMENT);
    assert(fsm_mode_init(APP_MODE_AUTOMATIC, &initial) == APP_NOT_READY);
    assert(fsm_mode_last_status() == APP_NOT_READY);

    assert(button_bind_io(read_level, clock_ms) == APP_OK);
    assert(button_configure_active_low(0) == APP_OK);
    assert(button_init() == APP_OK);
    assert(traffic_light_bind_io(write_gpio) == APP_OK);
    assert(traffic_light_init() == APP_OK);
    software_timer_init();
    assert(fsm_automatic_enter(&invalid) == APP_INVALID_ARGUMENT && pins == ALL_RED);
    assert(fsm_automatic_enter(&initial) == APP_OK);
    for (unsigned i = 0; i < 15U; ++i) {
        software_timer_tick();
        fsm_automatic_task();
    }
    assert(pins == P3); /* Equal rounded durations skip the clearance phase. */
    fsm_automatic_exit();
    assert(fsm_mode_request(APP_MODE_AUTOMATIC) == APP_OK);
    assert(fsm_mode_init(APP_MODE_AUTOMATIC, &initial) == APP_BUSY);
    assert(fsm_mode_request((app_mode_t)99) == APP_INVALID_ARGUMENT);
    assert(pins == P1);
    ticks(10);
    assert(pins == P2);

    /* Btn1 wins. The queued BTN2 press and subsequent held events are flushed. */
    high[BUTTON_ID_1] = true;
    high[BUTTON_ID_2] = true;
    sample(3, false);
    fsm_mode_task();
    expect_mode(APP_MODE_MANUAL);
    assert(snapshot_called && pins == P2);
    assert(fsm_manual_get_phase(&phase) == APP_OK && phase.road_2 == TRAFFIC_YELLOW);
    sample(60, true);
    assert(pins == P2);
    high[BUTTON_ID_1] = false;
    high[BUTTON_ID_2] = false;
    sample(5, true);
    ticks(20);
    assert(pins == P2);
    press(BUTTON_ID_2, 60);
    assert(fsm_manual_get_phase(&phase) == APP_OK && phase.road_1 == TRAFFIC_GREEN);

    press(BUTTON_ID_USER, 160);
    expect_mode(APP_MODE_TUNING);
    assert(pins == ALL_RED);
    press(BUTTON_ID_3, 60); /* Initial press plus one repeat. */
    fsm_tuning_view_t view;
    assert(fsm_tuning_get(&view) == APP_OK && view.draft.red_ms == 170
        && view.draft.green_ms == 120);
    press(BUTTON_ID_USER, 160);
    expect_mode(APP_MODE_MANUAL);
    assert(pins == ALL_RED);
    assert(fsm_mode_request(APP_MODE_TUNING) == APP_OK);
    assert(fsm_tuning_get(&view) == APP_OK && view.draft.red_ms == 150);
    press(BUTTON_ID_3, 4);
    press(BUTTON_ID_3, 4);
    press(BUTTON_ID_USER, 4);
    expect_mode(APP_MODE_AUTOMATIC);
    ticks(11);
    assert(pins == P1);
    ticks(1);
    assert(pins == P2);
    ticks(4);
    assert(pins == P2);
    ticks(1);
    assert(pins == P3); /* Paired edits preserve the initial zero clearance. */

    assert(fsm_mode_request(APP_MODE_TUNING) == APP_OK);
    software_timer_id_t ids[32];
    unsigned used = 0;
    while (used < 32U && software_timer_start(100, &ids[used]) == APP_OK) {
        ++used;
    }
    /* Existing Automatic's timer was released on exit. */
    assert(used == 8);
    press(BUTTON_ID_3, 4);
    press(BUTTON_ID_USER, 4);
    assert(fsm_mode_get(&mode) == APP_NOT_READY);
    assert(fsm_mode_last_status() == APP_NO_CAPACITY && pins == ALL_RED);
    assert(software_timer_cancel(ids[0]) == APP_OK);
    assert(fsm_mode_request(APP_MODE_AUTOMATIC) == APP_OK);
    ticks(17);
    assert(pins == P3); /* Failed confirmation did not publish Red=180 ms. */

    /* Invalidate the live phase timer. Automatic stops safely and task
     * remains idle. Mode can recover through an explicit handoff. */
    software_timer_init();
    fsm_mode_task();
    assert(pins == ALL_RED);
    ticks(20);
    assert(pins == ALL_RED);
    expect_mode(APP_MODE_AUTOMATIC);
    assert(fsm_mode_request(APP_MODE_MANUAL) == APP_OK);
    expect_mode(APP_MODE_MANUAL);
    assert(pins == ALL_RED); /* Unavailable snapshot falls back safely. */
    assert(fsm_mode_request(APP_MODE_AUTOMATIC) == APP_OK && pins == P1);
    puts("Mode integration tests passed");
    return 0;
}
