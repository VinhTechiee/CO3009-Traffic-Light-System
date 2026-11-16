#ifndef CO3009_BUTTON_H
#define CO3009_BUTTON_H

#include "global.h"

app_status_t button_init(void);
/* Scheduled bounded sampling/debounce step. */
void button_task(void);
/* Sole foreground consumer; false if empty or output is NULL. */
bool button_take_event(button_event_t *event);

#endif /* CO3009_BUTTON_H */
