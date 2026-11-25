#ifndef CO3009_SOFTWARE_TIMER_H
#define CO3009_SOFTWARE_TIMER_H

#include "global.h"

typedef uint32_t software_timer_id_t;
typedef uint64_t (*software_timer_clock_fn_t)(void);
/* Foreground-only timers. Use the standalone tick or bind a monotonic clock. */
void software_timer_init(void);
app_status_t software_timer_init_with_clock(software_timer_clock_fn_t clock_ticks);
/* One-shot, duration >= 1 tick; output ID valid on success only. */
app_status_t software_timer_start(uint32_t duration_ticks, software_timer_id_t *id);
app_status_t software_timer_cancel(software_timer_id_t id);
/* Sticky expiry until cancel; invalid IDs return APP_NOT_FOUND. */
app_status_t software_timer_is_expired(software_timer_id_t id, bool *expired);
/* Standalone clock: advance one tick. Bound clock: sample elapsed ticks;
 * optional because start and expiry queries sample the bound clock. */
void software_timer_tick(void);

#endif /* CO3009_SOFTWARE_TIMER_H */
