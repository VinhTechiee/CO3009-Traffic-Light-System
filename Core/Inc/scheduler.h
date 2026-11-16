#ifndef CO3009_SCHEDULER_H
#define CO3009_SCHEDULER_H

#include "global.h"

typedef void (*sch_task_fn_t)(void);
typedef uint32_t sch_task_id_t;
void SCH_Init(void);
/* Tick units: delay >= 1; period == 0 means one-shot. ID written on success. */
app_status_t SCH_Add_Task(sch_task_fn_t task, uint32_t delay_ticks,
                          uint32_t period_ticks, sch_task_id_t *id);
app_status_t SCH_Delete_Task(sch_task_id_t id);
/* ISR: O(1) relative-delay head update, never execute callbacks. */
void SCH_Update(void);
/* Foreground only; callbacks must return promptly. */
void SCH_Dispatch_Tasks(void);

#endif /* CO3009_SCHEDULER_H */
