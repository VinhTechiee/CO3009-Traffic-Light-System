#ifndef CO3009_FSM_MODE_H
#define CO3009_FSM_MODE_H

#include "global.h"

/* Explicit initial mode; copy configuration, retain no caller pointer. */
app_status_t fsm_mode_init(app_mode_t initial_mode, const timing_config_t *config);
app_status_t fsm_mode_request(app_mode_t mode);
/* APP_NOT_READY before successful initialization. */
app_status_t fsm_mode_get(app_mode_t *mode);
/* Sole button-event consumer; routes events and steps active FSM only. */
void fsm_mode_task(void);

#endif /* CO3009_FSM_MODE_H */
