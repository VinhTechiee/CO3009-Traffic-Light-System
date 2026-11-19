#include "fsm_manual.h"

#include "button.h"
#include "traffic_light.h"

#include <stddef.h>

typedef enum {
    FSM_MANUAL_INACTIVE,
    FSM_MANUAL_P1,
    FSM_MANUAL_P2,
    FSM_MANUAL_P3,
    FSM_MANUAL_P4,
    FSM_MANUAL_ALL_RED
} fsm_manual_phase_t;

static const traffic_state_t fsm_manual_phases[] = {
    [FSM_MANUAL_P1] = {TRAFFIC_RED, TRAFFIC_GREEN},
    [FSM_MANUAL_P2] = {TRAFFIC_RED, TRAFFIC_YELLOW},
    [FSM_MANUAL_P3] = {TRAFFIC_GREEN, TRAFFIC_RED},
    [FSM_MANUAL_P4] = {TRAFFIC_YELLOW, TRAFFIC_RED},
    [FSM_MANUAL_ALL_RED] = {TRAFFIC_RED, TRAFFIC_RED}
};

static fsm_manual_phase_t fsm_manual_phase = FSM_MANUAL_INACTIVE;
static uint8_t fsm_manual_pending_presses;

static fsm_manual_phase_t fsm_manual_find_phase(const traffic_state_t *pair)
{
    for (fsm_manual_phase_t phase = FSM_MANUAL_P1; phase <= FSM_MANUAL_P4; ++phase) {
        const traffic_state_t *colors = &fsm_manual_phases[phase];

        if (colors->road_1 == pair->road_1 && colors->road_2 == pair->road_2) {
            return phase;
        }
    }
    return FSM_MANUAL_ALL_RED;
}

app_status_t fsm_manual_enter(const traffic_state_t *current_phase)
{
    fsm_manual_phase_t phase;
    app_status_t status;

    if (current_phase == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (fsm_manual_phase != FSM_MANUAL_INACTIVE) {
        return APP_BUSY;
    }

    phase = fsm_manual_find_phase(current_phase);
    status = traffic_light_apply(&fsm_manual_phases[phase]);
    if (status != APP_OK) {
        if (phase != FSM_MANUAL_ALL_RED) {
            (void)traffic_light_apply(&fsm_manual_phases[FSM_MANUAL_ALL_RED]);
        }
        return status;
    }

    button_flush(BUTTON_MASK(BUTTON_ID_2));
    fsm_manual_phase = phase;
    fsm_manual_pending_presses = 0U;
    return APP_OK;
}

void fsm_manual_exit(void)
{
    if (fsm_manual_phase != FSM_MANUAL_INACTIVE) {
        fsm_manual_phase = FSM_MANUAL_INACTIVE;
        fsm_manual_pending_presses = 0U;
        button_flush(BUTTON_MASK(BUTTON_ID_2));
    }
}

void fsm_manual_on_button(const button_event_t *event)
{
    if (fsm_manual_phase != FSM_MANUAL_INACTIVE && event != NULL && event->id == BUTTON_ID_2
        && event->kind == BUTTON_PRESS && fsm_manual_pending_presses < UINT8_MAX) {
        ++fsm_manual_pending_presses;
    }
}

void fsm_manual_task(void)
{
    fsm_manual_phase_t next;

    if (fsm_manual_pending_presses == 0U) {
        return;
    }

    switch (fsm_manual_phase) {
        case FSM_MANUAL_P1:
            next = FSM_MANUAL_P2;
            break;
        case FSM_MANUAL_P2:
            next = FSM_MANUAL_P3;
            break;
        case FSM_MANUAL_P3:
            next = FSM_MANUAL_P4;
            break;
        case FSM_MANUAL_P4:
        case FSM_MANUAL_ALL_RED:
            next = FSM_MANUAL_P1;
            break;
        case FSM_MANUAL_INACTIVE:
        default:
            return;
    }

    --fsm_manual_pending_presses;
    if (traffic_light_apply(&fsm_manual_phases[next]) == APP_OK) {
        fsm_manual_phase = next;
        return;
    }

    fsm_manual_pending_presses = 0U;
    button_flush(BUTTON_MASK(BUTTON_ID_2));
    if (traffic_light_apply(&fsm_manual_phases[FSM_MANUAL_ALL_RED]) == APP_OK) {
        fsm_manual_phase = FSM_MANUAL_ALL_RED;
    } else {
        fsm_manual_phase = FSM_MANUAL_INACTIVE;
    }
}

app_status_t fsm_manual_get_phase(traffic_state_t *phase)
{
    if (phase == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (fsm_manual_phase == FSM_MANUAL_INACTIVE) {
        return APP_NOT_READY;
    }

    *phase = fsm_manual_phases[fsm_manual_phase];
    return APP_OK;
}
