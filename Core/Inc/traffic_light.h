#ifndef CO3009_TRAFFIC_LIGHT_H
#define CO3009_TRAFFIC_LIGHT_H

#include "global.h"

/* Establish board-verified safe outputs before APP_OK. */
app_status_t traffic_light_init(void);
/* Whole pair: reject invalid colors/dual green without writes.
 * Physical write ordering must also prevent transient dual green. */
app_status_t traffic_light_apply(const traffic_state_t *state);

#endif /* CO3009_TRAFFIC_LIGHT_H */
