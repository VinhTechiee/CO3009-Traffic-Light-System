#ifndef CO3009_SCHEDULER_H
#define CO3009_SCHEDULER_H

#include "global.h"

typedef void (*sch_task_fn_t)(void);
typedef uint32_t sch_task_id_t;
/* Foreground initialization; ignored if a callback is in progress. */
void SCH_Init(void);
/* Foreground, tick units: delay >= 1; period == 0 means one-shot.
 * ID written on success. */
app_status_t SCH_Add_Task(sch_task_fn_t task, uint32_t delay_ticks,
                          uint32_t period_ticks, sch_task_id_t *id);
app_status_t SCH_Delete_Task(sch_task_id_t id);
/* ISR: O(1) elapsed tick update, never execute callbacks. */
void SCH_Update(void);
/* Atomic elapsed tick snapshot taken in the foreground. */
uint64_t SCH_Now(void);
/* Foreground only; callbacks must return promptly. Missed periodic runs
 * coalesce once per pass and future runs keep their original tick phase. */
void SCH_Dispatch_Tasks(void);

#endif /* CO3009_SCHEDULER_H */
