#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "fsm_tuning.h"

static fsm_tuning_view_t act(fsm_tuning_action_t action, button_event_kind_t kind)
{
    const button_event_t event = {(button_id_t)action, kind};
    fsm_tuning_view_t view;
    fsm_tuning_on_button(&event);
    fsm_tuning_task();
    assert(fsm_tuning_get(&view) == APP_OK);
    return view;
}

int main(void)
{
    fsm_tuning_policy_t policy = {{300, 700}, {200, 500}, {100, 200}, 100, TRAFFIC_GREEN};
    const timing_config_t initial = {400, 100, 300};
    const timing_config_t invalid = {399, 100, 300};
    fsm_tuning_view_t view;
    assert(fsm_tuning_get(&view) == APP_NOT_READY);
    assert(fsm_tuning_enter(&initial) == APP_NOT_READY);
    assert(fsm_tuning_configure(&policy) == APP_OK);
    policy.step_ms = 1;
    assert(fsm_tuning_enter(&invalid) == APP_INVALID_ARGUMENT);
    assert(fsm_tuning_enter(&initial) == APP_OK);
    assert(fsm_tuning_configure(&policy) == APP_BUSY);

    view = act(FSM_TUNING_INCREASE, BUTTON_RELEASE);
    assert(view.selected == TRAFFIC_GREEN && view.draft.red_ms == 400);
    view = act(FSM_TUNING_INCREASE, BUTTON_PRESS);
    assert(view.draft.red_ms == 500 && view.draft.green_ms == 400 && view.draft.yellow_ms == 100);
    view = act(FSM_TUNING_NEXT, BUTTON_PRESS);
    assert(view.selected == TRAFFIC_YELLOW);
    view = act(FSM_TUNING_INCREASE, BUTTON_PRESS);
    assert(view.draft.red_ms == 600 && view.draft.green_ms == 400 && view.draft.yellow_ms == 200);
    view = act(FSM_TUNING_INCREASE, BUTTON_PRESS);
    assert(view.edit_status == APP_INVALID_ARGUMENT && view.draft.red_ms == 600
           && view.draft.yellow_ms == 200);
    view = act(FSM_TUNING_DECREASE, BUTTON_PRESS);
    assert(view.draft.red_ms == 500 && view.draft.green_ms == 400 && view.draft.yellow_ms == 100);
    view = act(FSM_TUNING_DECREASE, BUTTON_PRESS);
    assert(view.edit_status == APP_INVALID_ARGUMENT && view.draft.red_ms == 500
           && view.draft.yellow_ms == 100);
    view = act(FSM_TUNING_NEXT, BUTTON_PRESS);
    assert(view.selected == TRAFFIC_GREEN);
    view = act(FSM_TUNING_DECREASE, BUTTON_PRESS);
    assert(view.draft.red_ms == 400 && view.draft.green_ms == 300 && view.draft.yellow_ms == 100);
    assert(act(FSM_TUNING_CANCEL, BUTTON_PRESS).result == FSM_TUNING_CANCELLED);

    fsm_tuning_exit();
    assert(fsm_tuning_enter(&initial) == APP_OK);
    view = act(FSM_TUNING_CONFIRM, BUTTON_PRESS);
    assert(view.result == FSM_TUNING_CONFIRMED && view.draft.red_ms == 400);
    assert(act(FSM_TUNING_DECREASE, BUTTON_PRESS).draft.red_ms == 400);
    fsm_tuning_exit();

    /* A range-valid adjustment can still break the rounded tick relation. */
    const fsm_tuning_policy_t fine = {{1, 100}, {1, 100}, {1, 100}, 1, TRAFFIC_RED};
    const timing_config_t exact = {20, 10, 10};
    const timing_config_t extra = {31, 10, 10};
    const timing_config_t too_short_ms = {19, 10, 10};
    const timing_config_t too_short_ticks = {22, 11, 11};
    const timing_config_t extra_but_too_short_ticks = {30, 11, 11};
    const timing_config_t rounded_ok = {31, 11, 11};
    const timing_config_t out_of_range = {101, 10, 10};
    const timing_config_t edit_boundary = {20, 9, 11};
    assert(fsm_tuning_validate(&fine, &exact) == APP_OK);
    assert(fsm_tuning_validate(&fine, &extra) == APP_OK);
    assert(fsm_tuning_validate(&fine, &too_short_ms) == APP_INVALID_ARGUMENT);
    assert(fsm_tuning_validate(&fine, &too_short_ticks) == APP_INVALID_ARGUMENT);
    assert(fsm_tuning_validate(&fine, &extra_but_too_short_ticks) == APP_INVALID_ARGUMENT);
    assert(fsm_tuning_validate(&fine, &rounded_ok) == APP_OK);
    assert(fsm_tuning_validate(&fine, &out_of_range) == APP_INVALID_ARGUMENT);
    const timing_config_t before_tick_failure = {.red_ms = 31, .green_ms = 20, .yellow_ms = 11};
    assert(fsm_tuning_configure(&fine) == APP_OK);
    assert(fsm_tuning_enter(&edit_boundary) == APP_INVALID_ARGUMENT);
    assert(fsm_tuning_enter(&before_tick_failure) == APP_OK);
    (void)act(FSM_TUNING_SELECT_GREEN, BUTTON_PRESS);
    view = act(FSM_TUNING_INCREASE, BUTTON_PRESS);
    assert(view.edit_status == APP_INVALID_ARGUMENT
           && view.draft.red_ms == 31 && view.draft.green_ms == 20);
    assert(act(FSM_TUNING_CONFIRM, BUTTON_PRESS).result == FSM_TUNING_CONFIRMED);
    fsm_tuning_exit();

    policy = (fsm_tuning_policy_t){{1, UINT32_MAX}, {1, UINT32_MAX},
                                 {1, UINT32_MAX}, 1, TRAFFIC_YELLOW};
    const timing_config_t maximum = {UINT32_MAX, UINT32_MAX - 5U, 1};
    const timing_config_t sum_overflow = {UINT32_MAX, UINT32_MAX, 1};
    assert(fsm_tuning_validate(&policy, &sum_overflow) == APP_INVALID_ARGUMENT);
    assert(fsm_tuning_configure(&policy) == APP_OK);
    assert(fsm_tuning_enter(&maximum) == APP_OK);
    view = act(FSM_TUNING_INCREASE, BUTTON_PRESS);
    assert(view.edit_status == APP_INVALID_ARGUMENT && view.draft.red_ms == UINT32_MAX
           && view.draft.green_ms == 1);
    (void)act(FSM_TUNING_SELECT_YELLOW, BUTTON_PRESS);
    view = act(FSM_TUNING_DECREASE, BUTTON_PRESS);
    assert(view.draft.red_ms == UINT32_MAX - 1U && view.draft.yellow_ms == UINT32_MAX - 6U);
    puts("Tuning tests passed");
    return 0;
}
