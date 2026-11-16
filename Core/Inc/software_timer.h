#ifndef CO3009_SOFTWARE_TIMER_H
#define CO3009_SOFTWARE_TIMER_H

#include "global.h"

typedef uint32_t software_timer_id_t;
void software_timer_init(void);
/* One-shot, duration >= 1 tick; output ID valid on success only. */
app_status_t software_timer_start(uint32_t duration_ticks, software_timer_id_t *id);
app_status_t software_timer_cancel(software_timer_id_t id);
/* Sticky expiry until cancel; invalid IDs return APP_NOT_FOUND. */
app_status_t software_timer_is_expired(software_timer_id_t id, bool *expired);
/* Scheduled foreground callback, exactly once per agreed timer tick. */
void software_timer_tick(void);

#endif /* CO3009_SOFTWARE_TIMER_H */
