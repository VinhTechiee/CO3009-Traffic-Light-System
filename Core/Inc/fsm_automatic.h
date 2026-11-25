#ifndef CO3009_FSM_AUTOMATIC_H
#define CO3009_FSM_AUTOMATIC_H

#include "global.h"

/* Copy validated config on successful entry; reject NULL. */
app_status_t fsm_automatic_enter(const timing_config_t *config);
void fsm_automatic_exit(void);
/* Copy the last successfully applied phase while Automatic is active. */
app_status_t fsm_automatic_get_phase(traffic_state_t *phase);
/* Foreground event delivery by fsm_mode, no physical button mappings. */
void fsm_automatic_on_button(const button_event_t *event);
/* Bounded step, invoked only while active. */
void fsm_automatic_task(void);

#endif /* CO3009_FSM_AUTOMATIC_H */
