#ifndef CO3009_FSM_AUTOMATIC_H
#define CO3009_FSM_AUTOMATIC_H

#include "global.h"

typedef struct {
    traffic_state_t colors;
    uint32_t road1_seconds;
    uint32_t road2_seconds;
} fsm_automatic_view_t;

/* Copy validated config on successful entry; reject NULL. */
app_status_t fsm_automatic_enter(const timing_config_t *config);
void fsm_automatic_exit(void);
/* Copy the last successfully applied phase while Automatic is active. */
app_status_t fsm_automatic_get_phase(traffic_state_t *phase);
/* Seconds until each road changes color, rounded up to whole seconds. */
app_status_t fsm_automatic_get_view(fsm_automatic_view_t *view);
/* Foreground event delivery by fsm_mode, no physical button mappings. */
void fsm_automatic_on_button(const button_event_t *event);
/* Bounded step, invoked only while active. */
void fsm_automatic_task(void);

#endif /* CO3009_FSM_AUTOMATIC_H */
