#include "scheduler.h"

#include "stm32f1xx_hal.h"   /* CMSIS PRIMASK/IRQ intrinsics */
#include <stddef.h>
#include <stdint.h>

/* Fixed storage keeps memory use deterministic on the STM32. */
#ifndef SCH_MAX_TASKS
#define SCH_MAX_TASKS 16U
#endif

typedef struct
{
    sch_task_fn_t function;
    volatile uint32_t delay;  /* Relative delay from the previous task. */
    uint32_t      period;      /* 0 = one-shot; >0 = periodic. */
    volatile uint8_t run_me;
    sch_task_id_t id;
} sch_task_t;

static sch_task_t g_tasks[SCH_MAX_TASKS];
static volatile uint32_t g_task_count;
static sch_task_id_t g_next_id = 1U;
static volatile uint8_t g_initialized;

/* A periodic callback is temporarily outside the list while it executes.
 * Reserve one slot so the callback cannot fill the list and prevent its own
 * periodic re-insertion. Delete_Task may cancel this in-flight task. */
static uint8_t g_dispatching;
static uint8_t g_inflight_active;
static uint8_t g_inflight_cancelled;
static sch_task_t g_inflight_task;

static uint32_t sch_lock(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void sch_unlock(uint32_t primask)
{
    __set_PRIMASK(primask);
}

static uint8_t sch_id_in_use(sch_task_id_t id)
{
    uint32_t i;

    for (i = 0U; i < g_task_count; ++i)
    {
        if (g_tasks[i].id == id)
        {
            return 1U;
        }
    }

    if ((g_inflight_active != 0U) && (g_inflight_task.id == id))
    {
        return 1U;
    }

    return 0U;
}

static sch_task_id_t sch_allocate_id(void)
{
    uint32_t attempts;

    /* At most SCH_MAX_TASKS + 1 attempts are needed to find a free ID. */
    for (attempts = 0U; attempts <= SCH_MAX_TASKS; ++attempts)
    {
        sch_task_id_t candidate = g_next_id++;

        if (g_next_id == 0U)
        {
            g_next_id = 1U;
        }
        if (candidate == 0U)
        {
            candidate = g_next_id++;
            if (g_next_id == 0U)
            {
                g_next_id = 1U;
            }
        }
        if ((candidate != 0U) && (sch_id_in_use(candidate) == 0U))
        {
            return candidate;
        }
    }

    return 0U;
}

/* Insert a task into the relative-delay list. Caller must hold the lock.
 * Equal absolute deadlines are kept in insertion order (stable ordering). */
static uint8_t sch_insert_locked(const sch_task_t *task, uint32_t delay_ticks)
{
    uint32_t index = 0U;
    uint32_t remaining = delay_ticks;
    uint32_t i;

    if (g_task_count >= SCH_MAX_TASKS)
    {
        return 0U;
    }

    while (index < g_task_count)
    {
        if (remaining < g_tasks[index].delay)
        {
            break;
        }

        remaining -= g_tasks[index].delay;
        ++index;
    }

    if (index < g_task_count)
    {
        /* The new task takes 'remaining' ticks from the next task's delay. */
        if (g_tasks[index].delay < remaining)
        {
            return 0U; /* Defensive consistency check. */
        }
        g_tasks[index].delay -= remaining;
    }

    for (i = g_task_count; i > index; --i)
    {
        g_tasks[i] = g_tasks[i - 1U];
    }

    g_tasks[index] = *task;
    g_tasks[index].delay = remaining;
    g_tasks[index].run_me = 0U;
    ++g_task_count;

    return 1U;
}

void SCH_Init(void)
{
    uint32_t primask = sch_lock();
    uint32_t i;

    for (i = 0U; i < SCH_MAX_TASKS; ++i)
    {
        g_tasks[i].function = NULL;
        g_tasks[i].delay = 0U;
        g_tasks[i].period = 0U;
        g_tasks[i].run_me = 0U;
        g_tasks[i].id = 0U;
    }

    g_task_count = 0U;
    g_next_id = 1U;
    g_dispatching = 0U;
    g_inflight_active = 0U;
    g_inflight_cancelled = 0U;
    g_inflight_task.function = NULL;
    g_inflight_task.delay = 0U;
    g_inflight_task.period = 0U;
    g_inflight_task.run_me = 0U;
    g_inflight_task.id = 0U;
    g_initialized = 1U;

    sch_unlock(primask);
}

app_status_t SCH_Add_Task(sch_task_fn_t task,
                          uint32_t delay_ticks,
                          uint32_t period_ticks,
                          sch_task_id_t *id)
{
    uint32_t primask;
    uint32_t reserved;
    sch_task_t new_task;
    sch_task_id_t new_id;
    uint8_t inserted;

    if ((task == NULL) || (id == NULL) || (delay_ticks == 0U))
    {
        return APP_INVALID_ARGUMENT;
    }

    primask = sch_lock();

    if (g_initialized == 0U)
    {
        sch_unlock(primask);
        return APP_NOT_READY;
    }

    reserved = ((g_inflight_active != 0U) &&
                (g_inflight_cancelled == 0U) &&
                (g_inflight_task.period > 0U)) ? 1U : 0U;

    if ((g_task_count + reserved) >= SCH_MAX_TASKS)
    {
        sch_unlock(primask);
        return APP_NO_CAPACITY;
    }

    new_id = sch_allocate_id();
    if (new_id == 0U)
    {
        sch_unlock(primask);
        return APP_NO_CAPACITY;
    }

    new_task.function = task;
    new_task.delay = delay_ticks;
    new_task.period = period_ticks;
    new_task.run_me = 0U;
    new_task.id = new_id;

    inserted = sch_insert_locked(&new_task, delay_ticks);
    if (inserted == 0U)
    {
        sch_unlock(primask);
        return APP_BUSY;
    }

    *id = new_id; /* Output ID is written only after successful insertion. */
    sch_unlock(primask);
    return APP_OK;
}

app_status_t SCH_Delete_Task(sch_task_id_t id)
{
    uint32_t primask;
    uint32_t index;

    if (id == 0U)
    {
        return APP_INVALID_ARGUMENT;
    }

    primask = sch_lock();

    if (g_initialized == 0U)
    {
        sch_unlock(primask);
        return APP_NOT_READY;
    }

    if ((g_inflight_active != 0U) && (g_inflight_task.id == id))
    {
        g_inflight_cancelled = 1U;
        sch_unlock(primask);
        return APP_OK;
    }

    for (index = 0U; index < g_task_count; ++index)
    {
        if (g_tasks[index].id == id)
        {
            break;
        }
    }

    if (index >= g_task_count)
    {
        sch_unlock(primask);
        return APP_NOT_FOUND;
    }

    /* Relative-delay invariant: removing an entry transfers its delay to
     * its successor, preserving the successor's absolute deadline. */
    if ((index + 1U) < g_task_count)
    {
        if (UINT32_MAX - g_tasks[index + 1U].delay < g_tasks[index].delay)
        {
            sch_unlock(primask);
            return APP_BUSY; /* Avoid silent unsigned overflow. */
        }
        g_tasks[index + 1U].delay += g_tasks[index].delay;
    }

    for (; (index + 1U) < g_task_count; ++index)
    {
        g_tasks[index] = g_tasks[index + 1U];
    }

    --g_task_count;
    g_tasks[g_task_count].function = NULL;
    g_tasks[g_task_count].delay = 0U;
    g_tasks[g_task_count].period = 0U;
    g_tasks[g_task_count].run_me = 0U;
    g_tasks[g_task_count].id = 0U;

    sch_unlock(primask);
    return APP_OK;
}

/* Called only from the TIM2 ISR. Foreground list mutations mask interrupts. */
void SCH_Update(void)
{
    if ((g_initialized == 0U) || (g_task_count == 0U))
    {
        return;
    }

    if (g_tasks[0].delay > 0U)
    {
        --g_tasks[0].delay;
    }

    if (g_tasks[0].delay == 0U)
    {
        g_tasks[0].run_me = 1U;
    }
}

void SCH_Dispatch_Tasks(void)
{
    uint32_t primask;

    primask = sch_lock();
    if ((g_initialized == 0U) || (g_dispatching != 0U))
    {
        sch_unlock(primask);
        return;
    }
    g_dispatching = 1U;
    sch_unlock(primask);

    for (;;)
    {
        sch_task_t current;
        uint8_t has_task = 0U;
        uint8_t periodic;

        primask = sch_lock();

        if ((g_initialized != 0U) && (g_task_count > 0U) &&
            ((g_tasks[0].run_me != 0U) || (g_tasks[0].delay == 0U)))
        {
            current = g_tasks[0];

            /* The head is due, so its relative delay is zero. Removing it
             * does not require changing the next entry's relative delay. */
            for (uint32_t i = 0U; (i + 1U) < g_task_count; ++i)
            {
                g_tasks[i] = g_tasks[i + 1U];
            }
            --g_task_count;
            g_tasks[g_task_count].function = NULL;
            g_tasks[g_task_count].delay = 0U;
            g_tasks[g_task_count].period = 0U;
            g_tasks[g_task_count].run_me = 0U;
            g_tasks[g_task_count].id = 0U;

            periodic = (current.period > 0U) ? 1U : 0U;
            if (periodic != 0U)
            {
                g_inflight_task = current;
                g_inflight_active = 1U;
                g_inflight_cancelled = 0U;
            }
            has_task = 1U;
        }
        else
        {
            periodic = 0U;
        }

        sch_unlock(primask);

        if (has_task == 0U)
        {
            break;
        }

        /* Never execute user callbacks with interrupts disabled. */
        current.function();

        if (periodic != 0U)
        {
            primask = sch_lock();

            if ((g_inflight_active != 0U) &&
                (g_inflight_task.id == current.id))
            {
                uint8_t cancelled = g_inflight_cancelled;
                g_inflight_active = 0U;
                g_inflight_cancelled = 0U;

                if ((cancelled == 0U) && (g_initialized != 0U))
                {
                    /* One slot was reserved while the callback ran. */
                    (void)sch_insert_locked(&current, current.period);
                }
            }

            sch_unlock(primask);
        }
    }

    primask = sch_lock();
    g_dispatching = 0U;
    sch_unlock(primask);
}
