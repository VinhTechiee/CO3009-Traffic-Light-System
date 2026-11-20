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
    assert(view.draft.red_ms == 400);
    view = act(FSM_TUNING_INCREASE, BUTTON_PRESS);
    assert(view.draft.red_ms == 500 && view.draft.green_ms == 400);
    view = act(FSM_TUNING_NEXT, BUTTON_PRESS);
    assert(view.selected == TRAFFIC_GREEN);
    view = act(FSM_TUNING_DECREASE, BUTTON_PRESS);
    assert(view.draft.green_ms == 300);
    view = act(FSM_TUNING_SELECT_YELLOW, BUTTON_PRESS);
    assert(view.selected == TRAFFIC_YELLOW);
    view = act(FSM_TUNING_INCREASE, BUTTON_PRESS);
    assert(view.draft.yellow_ms == 200 && view.draft.red_ms == 500);
    view = act(FSM_TUNING_INCREASE, BUTTON_PRESS);
    assert(view.edit_status == APP_INVALID_ARGUMENT && view.draft.yellow_ms == 200);
    assert(act(FSM_TUNING_CANCEL, BUTTON_PRESS).result == FSM_TUNING_CANCELLED);

    fsm_tuning_exit();
    assert(fsm_tuning_enter(&initial) == APP_OK);
    view = act(FSM_TUNING_CONFIRM, BUTTON_PRESS);
    assert(view.result == FSM_TUNING_CONFIRMED && view.draft.red_ms == 400);
    assert(act(FSM_TUNING_DECREASE, BUTTON_PRESS).draft.red_ms == 400);
    fsm_tuning_exit();

    policy = (fsm_tuning_policy_t){{1, UINT32_MAX}, {1, UINT32_MAX},
                                 {1, UINT32_MAX}, 1, TRAFFIC_YELLOW};
    const timing_config_t maximum = {UINT32_MAX, UINT32_MAX - 1U, 1};
    assert(fsm_tuning_configure(&policy) == APP_OK);
    assert(fsm_tuning_enter(&maximum) == APP_OK);
    view = act(FSM_TUNING_INCREASE, BUTTON_PRESS);
    assert(view.edit_status == APP_INVALID_ARGUMENT && view.draft.red_ms == UINT32_MAX);
    assert(act(FSM_TUNING_DECREASE, BUTTON_PRESS).draft.yellow_ms == UINT32_MAX - 2U);
    puts("Tuning tests passed");
    return 0;
}
