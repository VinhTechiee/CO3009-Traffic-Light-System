#include "fsm_mode.h"

#include "fsm_automatic.h"
#include "fsm_manual.h"
#include "traffic_light.h"

#include <stddef.h>

static const traffic_state_t fsm_mode_all_red = {TRAFFIC_RED, TRAFFIC_RED};

static struct {
    fsm_mode_config_t config;
    timing_config_t committed;
    app_mode_t current;
    app_status_t last_status;
    bool configured;
    bool initialized;
    bool active;
} fsm_mode = {.last_status = APP_NOT_READY};

static bool fsm_mode_valid(app_mode_t mode)
{
    return mode == APP_MODE_AUTOMATIC || mode == APP_MODE_MANUAL || mode == APP_MODE_TUNING;
}

static bool fsm_mode_destination(app_mode_t mode)
{
    return mode == APP_MODE_AUTOMATIC || mode == APP_MODE_MANUAL;
}

static bool fsm_mode_adjustment_binding(const fsm_mode_binding_t *binding)
{
    return binding->kind == FSM_MODE_TUNING_ACTION
        && (binding->button == BUTTON_ID_3 || binding->button == BUTTON_ID_4)
        && (binding->action == FSM_TUNING_INCREASE || binding->action == FSM_TUNING_DECREASE);
}

static bool fsm_mode_binding_valid(app_mode_t source, const fsm_mode_binding_t *binding)
{
    return binding->button < BUTTON_ID_COUNT
        && (source != APP_MODE_MANUAL || binding->button != BUTTON_ID_2)
        && (binding->trigger == BUTTON_EVENT_PRESS
            || ((binding->button == BUTTON_ID_2 || binding->button == BUTTON_ID_USER)
                && (binding->trigger == BUTTON_EVENT_SHORT || binding->trigger == BUTTON_EVENT_LONG))
            || (binding->trigger == BUTTON_EVENT_REPEAT && fsm_mode_adjustment_binding(binding)))
        && ((binding->kind == FSM_MODE_SWITCH && fsm_mode_valid(binding->mode)
             && binding->mode != source)
            || (binding->kind == FSM_MODE_TUNING_ACTION && source == APP_MODE_TUNING
                && (unsigned)binding->action < FSM_TUNING_ACTION_COUNT));
}

static bool fsm_mode_conflicting_bindings(const fsm_mode_binding_t *first,
                                          const fsm_mode_binding_t *second)
{
    if (second->kind == FSM_MODE_UNBOUND || first->button != second->button) {
        return false;
    }
    if (first->trigger == second->trigger) {
        return true;
    }
    if (fsm_mode_adjustment_binding(first) && fsm_mode_adjustment_binding(second)
        && first->action == second->action) {
        return false;
    }
    return first->trigger == BUTTON_EVENT_PRESS || second->trigger == BUTTON_EVENT_PRESS;
}

app_status_t fsm_mode_configure(const fsm_mode_config_t *config)
{
    if (config == NULL || !fsm_mode_destination(config->after_confirm)
        || !fsm_mode_destination(config->after_cancel)) {
        return APP_INVALID_ARGUMENT;
    }
    if (fsm_mode.active) {
        return APP_BUSY;
    }

    for (app_mode_t mode = APP_MODE_AUTOMATIC; mode <= APP_MODE_TUNING; ++mode) {
        const fsm_mode_binding_t *bindings = config->bindings[mode];
        for (size_t i = 0; i < FSM_MODE_BINDINGS_PER_MODE; ++i) {
            if (bindings[i].kind == FSM_MODE_UNBOUND) {
                continue;
            }
            if (!fsm_mode_binding_valid(mode, &bindings[i])) {
                return APP_INVALID_ARGUMENT;
            }
            for (size_t j = 0; j < i; ++j) {
                if (fsm_mode_conflicting_bindings(&bindings[i], &bindings[j])) {
                    return APP_INVALID_ARGUMENT;
                }
            }
        }
    }

    const app_status_t status = fsm_tuning_configure(&config->timing);
    if (status == APP_OK) {
        fsm_mode.config = *config;
        fsm_mode.configured = true;
        fsm_mode.initialized = false;
        fsm_mode.last_status = APP_NOT_READY;
    }
    return status;
}

static void fsm_mode_exit_active(void)
{
    switch (fsm_mode.current) {
    case APP_MODE_AUTOMATIC: fsm_automatic_exit(); break;
    case APP_MODE_MANUAL: fsm_manual_exit(); break;
    case APP_MODE_TUNING: fsm_tuning_exit(); break;
    }
    fsm_mode.active = false;
}

static app_status_t fsm_mode_transition(app_mode_t target, const timing_config_t *timing)
{
    traffic_state_t snapshot = fsm_mode_all_red;
    if (fsm_mode.active) {
        if (fsm_mode.current == APP_MODE_AUTOMATIC && target == APP_MODE_MANUAL
            && fsm_mode.config.automatic_phase != NULL
            && fsm_mode.config.automatic_phase(&snapshot) != APP_OK) {
            snapshot = fsm_mode_all_red;
        }
        fsm_mode_exit_active();
    }

    button_flush(BUTTON_MASK_ALL);
    app_status_t status = traffic_light_apply(&fsm_mode_all_red);
    if (status == APP_OK) {
        switch (target) {
        case APP_MODE_AUTOMATIC: status = fsm_automatic_enter(timing); break;
        case APP_MODE_MANUAL: status = fsm_manual_enter(&snapshot); break;
        case APP_MODE_TUNING: status = fsm_tuning_enter(timing); break;
        }
        if (status != APP_OK) {
            (void)traffic_light_apply(&fsm_mode_all_red);
        }
    }
    if (status == APP_OK) {
        fsm_mode.current = target;
        fsm_mode.active = true;
    }
    fsm_mode.last_status = status;
    return status;
}

app_status_t fsm_mode_init(app_mode_t initial_mode, const timing_config_t *config)
{
    if (!fsm_mode_valid(initial_mode) || config == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (fsm_mode.active) {
        return APP_BUSY;
    }
    if (!fsm_mode.configured) {
        return APP_NOT_READY;
    }

    const app_status_t status = fsm_tuning_validate(&fsm_mode.config.timing, config);
    if (status != APP_OK) {
        return status;
    }
    fsm_mode.committed = *config;
    fsm_mode.initialized = true;
    return fsm_mode_transition(initial_mode, &fsm_mode.committed);
}

app_status_t fsm_mode_request(app_mode_t mode)
{
    if (!fsm_mode_valid(mode)) {
        return APP_INVALID_ARGUMENT;
    }
    if (!fsm_mode.initialized) {
        return APP_NOT_READY;
    }
    if (fsm_mode.active && mode == fsm_mode.current) {
        return APP_OK;
    }
    return fsm_mode_transition(mode, &fsm_mode.committed);
}

app_status_t fsm_mode_get(app_mode_t *mode)
{
    if (mode == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (!fsm_mode.active) {
        return APP_NOT_READY;
    }
    *mode = fsm_mode.current;
    return APP_OK;
}

app_status_t fsm_mode_last_status(void)
{
    return fsm_mode.last_status;
}

/* True when a switch was attempted; end the tick at the transition boundary. */
static bool fsm_mode_route(const button_input_event_t *input)
{
    if (input->id >= BUTTON_ID_COUNT) {
        return false;
    }

    const fsm_mode_binding_t *bindings = fsm_mode.config.bindings[fsm_mode.current];
    bool reserved = false;
    for (size_t i = 0; i < FSM_MODE_BINDINGS_PER_MODE; ++i) {
        const fsm_mode_binding_t *binding = &bindings[i];
        if (binding->kind == FSM_MODE_UNBOUND || binding->button != input->id) {
            continue;
        }
        reserved = true;
        if (binding->trigger != input->kind) {
            continue;
        }
        if (binding->kind == FSM_MODE_SWITCH) {
            (void)fsm_mode_request(binding->mode);
            return true;
        }
        const button_event_t translated = {(button_id_t)binding->action, BUTTON_PRESS};
        fsm_tuning_on_button(&translated);
        return false;
    }

    if (!reserved && (input->kind == BUTTON_EVENT_PRESS || input->kind == BUTTON_EVENT_RELEASE)) {
        const button_event_t edge = {
            input->id, input->kind == BUTTON_EVENT_PRESS ? BUTTON_PRESS : BUTTON_RELEASE
        };
        if (fsm_mode.current == APP_MODE_MANUAL) {
            fsm_manual_on_button(&edge);
        } else if (fsm_mode.current == APP_MODE_AUTOMATIC) {
            fsm_automatic_on_button(&edge);
        }
    }
    return false;
}

void fsm_mode_task(void)
{
    button_input_event_t input;
    fsm_tuning_view_t view;
    if (!fsm_mode.active || (button_take_event(&input) && fsm_mode_route(&input))) {
        return;
    }

    switch (fsm_mode.current) {
    case APP_MODE_AUTOMATIC:
        fsm_automatic_task();
        break;
    case APP_MODE_MANUAL:
        fsm_manual_task();
        break;
    case APP_MODE_TUNING:
        fsm_tuning_task();
        if (fsm_tuning_get(&view) != APP_OK) {
            break;
        }
        if (view.result == FSM_TUNING_CONFIRMED) {
            if (fsm_mode_transition(fsm_mode.config.after_confirm, &view.draft) == APP_OK) {
                fsm_mode.committed = view.draft;
            }
        } else if (view.result == FSM_TUNING_CANCELLED) {
            (void)fsm_mode_request(fsm_mode.config.after_cancel);
        }
        break;
    }
}
