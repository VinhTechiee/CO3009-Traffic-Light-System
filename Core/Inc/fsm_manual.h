#ifndef CO3009_FSM_MANUAL_H
#define CO3009_FSM_MANUAL_H

#include "global.h"

/* Reapply the supplied Auto color pair. Invalid pairs fall back to all-red
 * and the first fresh BTN2 press selects P1. A NULL pointer is rejected.
 * Call after stopping Auto. Manual also flushes and blocks held BTN2 input. */
app_status_t fsm_manual_enter(const traffic_state_t *current_phase);
void fsm_manual_exit(void);
/* fsm_mode routes the BTN2 PRESS edge here. Manual never reads the FIFO. */
void fsm_manual_on_button(const button_event_t *event);
/* Advance at most one delivered press. The mode owner steps Manual only
 * while active. After a failed write, Manual attempts the all-red fallback. */
void fsm_manual_task(void);
/* Copy the currently applied pair, including the all-red fallback.
 * APP_NOT_READY when inactive. */
app_status_t fsm_manual_get_phase(traffic_state_t *phase);

#endif /* CO3009_FSM_MANUAL_H */
