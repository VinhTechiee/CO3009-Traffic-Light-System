#ifndef CO3009_FSM_TUNING_H
#define CO3009_FSM_TUNING_H

#include "global.h"

typedef enum {
    FSM_TUNING_SELECT_RED, FSM_TUNING_SELECT_GREEN, FSM_TUNING_SELECT_YELLOW,
    FSM_TUNING_NEXT, FSM_TUNING_INCREASE, FSM_TUNING_DECREASE,
    FSM_TUNING_CONFIRM, FSM_TUNING_CANCEL, FSM_TUNING_ACTION_COUNT
} fsm_tuning_action_t;

typedef struct {
    uint32_t min_ms;
    uint32_t max_ms;
} fsm_tuning_range_t;

typedef struct {
    fsm_tuning_range_t red, green, yellow;
    uint32_t step_ms;
    /* When editing red, this duration moves by the same step as red. */
    traffic_color_t red_adjusts;
} fsm_tuning_policy_t;

typedef enum {
    FSM_TUNING_EDITING, FSM_TUNING_CONFIRMED, FSM_TUNING_CANCELLED
} fsm_tuning_result_t;

typedef struct {
    timing_config_t draft;
    traffic_color_t selected;
    fsm_tuning_result_t result;
    app_status_t edit_status;
} fsm_tuning_view_t;

app_status_t fsm_tuning_validate(const fsm_tuning_policy_t *policy,
                                 const timing_config_t *config);
app_status_t fsm_tuning_configure(const fsm_tuning_policy_t *policy);
app_status_t fsm_tuning_enter(const timing_config_t *config);
void fsm_tuning_exit(void);
/* PRESS only. Mode translates physical IDs into fsm_tuning_action_t IDs. */
void fsm_tuning_on_button(const button_event_t *event);
void fsm_tuning_task(void);
app_status_t fsm_tuning_get(fsm_tuning_view_t *view);

#endif /* CO3009_FSM_TUNING_H */
