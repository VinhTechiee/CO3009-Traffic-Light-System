#ifndef CO3009_FSM_MODE_H
#define CO3009_FSM_MODE_H

#include "button.h"
#include "fsm_tuning.h"

#define FSM_MODE_BINDINGS_PER_MODE 7U

typedef enum {
    FSM_MODE_UNBOUND, FSM_MODE_SWITCH, FSM_MODE_TUNING_ACTION
} fsm_mode_binding_kind_t;

typedef struct {
    fsm_mode_binding_kind_t kind;
    button_id_t button;
    button_input_event_kind_t trigger;
    app_mode_t mode;             /* FSM_MODE_SWITCH */
    fsm_tuning_action_t action;  /* FSM_MODE_TUNING_ACTION */
} fsm_mode_binding_t;

typedef struct {
    fsm_tuning_policy_t timing;
    app_mode_t after_confirm;
    app_mode_t after_cancel;
    /* Optional proposed A adapter; snapshot before Automatic exit. */
    app_status_t (*automatic_phase)(traffic_state_t *phase);
    fsm_mode_binding_t bindings[3][FSM_MODE_BINDINGS_PER_MODE];
} fsm_mode_config_t;

/* Supply agreed policies and per-mode bindings before init. An all-zero row
 * leaves mode switching to explicit requests. Both outcome modes must be
 * Automatic or Manual. Manual BTN2 is reserved for its existing PRESS action. */
app_status_t fsm_mode_configure(const fsm_mode_config_t *config);
/* Explicit initial mode; copy configuration, retain no caller pointer. */
app_status_t fsm_mode_init(app_mode_t initial_mode, const timing_config_t *config);
/* Synchronous transition; on failure no mode is active. After a failed entry
 * an explicit request can retry using the last committed timings. */
app_status_t fsm_mode_request(app_mode_t mode);
app_status_t fsm_mode_get(app_mode_t *mode);
/* Most recent transition result, including a transition triggered by task. */
app_status_t fsm_mode_last_status(void);
/* Sole consumer; one input event per foreground invocation. */
void fsm_mode_task(void);

#endif /* CO3009_FSM_MODE_H */
