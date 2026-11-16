#ifndef CO3009_FSM_MANUAL_H
#define CO3009_FSM_MANUAL_H

#include "global.h"

/* Copy validated config on successful entry; reject NULL. */
app_status_t fsm_manual_enter(const timing_config_t *config);
void fsm_manual_exit(void);
/* Foreground event delivery by fsm_mode, no physical button mappings. */
void fsm_manual_on_button(const button_event_t *event);
/* Bounded step, invoked only while active. */
void fsm_manual_task(void);

#endif /* CO3009_FSM_MANUAL_H */
