#include "fsm_tuning.h"

#include <stddef.h>

static struct {
    fsm_tuning_policy_t policy;
    fsm_tuning_view_t view;
    fsm_tuning_action_t pending;
    bool configured;
    bool active;
} fsm_tuning = {.pending = FSM_TUNING_ACTION_COUNT};

static bool fsm_tuning_range_valid(fsm_tuning_range_t range)
{
    return range.min_ms > 0U && range.max_ms >= range.min_ms;
}

static bool fsm_tuning_contains(fsm_tuning_range_t range, uint32_t value)
{
    return value >= range.min_ms && value <= range.max_ms;
}

static bool fsm_tuning_policy_valid(const fsm_tuning_policy_t *policy)
{
    return policy != NULL && policy->step_ms > 0U
        && fsm_tuning_range_valid(policy->red)
        && fsm_tuning_range_valid(policy->green)
        && fsm_tuning_range_valid(policy->yellow)
        && (policy->red_adjusts == TRAFFIC_GREEN || policy->red_adjusts == TRAFFIC_YELLOW);
}

app_status_t fsm_tuning_validate(const fsm_tuning_policy_t *policy,
                                 const timing_config_t *config)
{
    if (!fsm_tuning_policy_valid(policy) || config == NULL
        || !fsm_tuning_contains(policy->red, config->red_ms)
        || !fsm_tuning_contains(policy->yellow, config->yellow_ms)
        || !fsm_tuning_contains(policy->green, config->green_ms)
        || (uint64_t)config->green_ms + config->yellow_ms != config->red_ms) {
        return APP_INVALID_ARGUMENT;
    }
    return APP_OK;
}

app_status_t fsm_tuning_configure(const fsm_tuning_policy_t *policy)
{
    if (!fsm_tuning_policy_valid(policy)) {
        return APP_INVALID_ARGUMENT;
    }
    if (fsm_tuning.active) {
        return APP_BUSY;
    }

    fsm_tuning.policy = *policy;
    fsm_tuning.configured = true;
    return APP_OK;
}

app_status_t fsm_tuning_enter(const timing_config_t *config)
{
    if (config == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (fsm_tuning.active) {
        return APP_BUSY;
    }
    if (!fsm_tuning.configured) {
        return APP_NOT_READY;
    }

    const app_status_t status = fsm_tuning_validate(&fsm_tuning.policy, config);
    if (status != APP_OK) {
        return status;
    }

    fsm_tuning.view = (fsm_tuning_view_t){*config, TRAFFIC_RED, FSM_TUNING_EDITING, APP_OK};
    fsm_tuning.pending = FSM_TUNING_ACTION_COUNT;
    fsm_tuning.active = true;
    return APP_OK;
}

void fsm_tuning_exit(void)
{
    fsm_tuning.active = false;
    fsm_tuning.pending = FSM_TUNING_ACTION_COUNT;
}

void fsm_tuning_on_button(const button_event_t *event)
{
    if (fsm_tuning.active && event != NULL && event->kind == BUTTON_PRESS
        && event->id < FSM_TUNING_ACTION_COUNT
        && fsm_tuning.view.result == FSM_TUNING_EDITING
        && fsm_tuning.pending == FSM_TUNING_ACTION_COUNT) {
        fsm_tuning.pending = (fsm_tuning_action_t)event->id;
    }
}

static app_status_t fsm_tuning_adjust(bool increase)
{
    timing_config_t candidate = fsm_tuning.view.draft;
    traffic_color_t color = fsm_tuning.view.selected;
    if (color == TRAFFIC_RED) {
        color = fsm_tuning.policy.red_adjusts;
    }
    uint32_t *duration = color == TRAFFIC_GREEN ? &candidate.green_ms : &candidate.yellow_ms;
    const int64_t step = fsm_tuning.policy.step_ms;
    const int64_t adjusted = (int64_t)*duration + (increase ? step : -step);
    if (adjusted <= 0 || adjusted > UINT32_MAX) {
        return APP_INVALID_ARGUMENT;
    }

    *duration = (uint32_t)adjusted;
    const uint64_t red = (uint64_t)candidate.green_ms + candidate.yellow_ms;
    if (red > UINT32_MAX) {
        return APP_INVALID_ARGUMENT;
    }
    candidate.red_ms = (uint32_t)red;
    const app_status_t status = fsm_tuning_validate(&fsm_tuning.policy, &candidate);
    if (status == APP_OK) {
        fsm_tuning.view.draft = candidate;
    }
    return status;
}

void fsm_tuning_task(void)
{
    if (!fsm_tuning.active || fsm_tuning.pending == FSM_TUNING_ACTION_COUNT) {
        return;
    }

    const fsm_tuning_action_t action = fsm_tuning.pending;
    fsm_tuning.pending = FSM_TUNING_ACTION_COUNT;
    fsm_tuning.view.edit_status = APP_OK;
    switch (action) {
    case FSM_TUNING_SELECT_RED:
        fsm_tuning.view.selected = TRAFFIC_RED;
        break;
    case FSM_TUNING_SELECT_GREEN:
        fsm_tuning.view.selected = TRAFFIC_GREEN;
        break;
    case FSM_TUNING_SELECT_YELLOW:
        fsm_tuning.view.selected = TRAFFIC_YELLOW;
        break;
    case FSM_TUNING_NEXT:
        switch (fsm_tuning.view.selected) {
        case TRAFFIC_RED: fsm_tuning.view.selected = TRAFFIC_GREEN; break;
        case TRAFFIC_GREEN: fsm_tuning.view.selected = TRAFFIC_YELLOW; break;
        default: fsm_tuning.view.selected = TRAFFIC_RED; break;
        }
        break;
    case FSM_TUNING_INCREASE:
    case FSM_TUNING_DECREASE:
        fsm_tuning.view.edit_status = fsm_tuning_adjust(action == FSM_TUNING_INCREASE);
        break;
    case FSM_TUNING_CONFIRM:
        fsm_tuning.view.edit_status =
            fsm_tuning_validate(&fsm_tuning.policy, &fsm_tuning.view.draft);
        if (fsm_tuning.view.edit_status == APP_OK) {
            fsm_tuning.view.result = FSM_TUNING_CONFIRMED;
        }
        break;
    case FSM_TUNING_CANCEL:
        fsm_tuning.view.result = FSM_TUNING_CANCELLED;
        break;
    default:
        break;
    }
}

app_status_t fsm_tuning_get(fsm_tuning_view_t *view)
{
    if (view == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (!fsm_tuning.active) {
        return APP_NOT_READY;
    }
    *view = fsm_tuning.view;
    return APP_OK;
}
