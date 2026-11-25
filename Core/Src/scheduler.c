#include "scheduler.h"

#ifndef SCH_HOST_TEST
#include "stm32f1xx_hal.h" /* CMSIS PRIMASK/IRQ intrinsics */
#endif
#include <stddef.h>

#ifndef SCH_MAX_TASKS
#define SCH_MAX_TASKS 16U
#endif

typedef struct
{
    sch_task_fn_t function;
    uint64_t delay; /* Relative to the previous deadline or g_queue_tick. */
    uint32_t period;
    sch_task_id_t id;
} sch_task_t;

/* Only g_ticks is shared with TIM2. All queue operations are foreground. */
static volatile uint64_t g_ticks;
static uint64_t g_queue_tick;
static sch_task_t g_tasks[SCH_MAX_TASKS];
static uint32_t g_task_count;
static sch_task_id_t g_next_id = 1U;
static sch_task_id_t g_inflight_id;
static bool g_initialized;
static bool g_dispatching;

static uint32_t sch_lock(void)
{
#ifndef SCH_HOST_TEST
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
#else
    /* Host fixtures emulate interrupt/foreground interleavings serially. */
    return 0U;
#endif
}

static void sch_unlock(uint32_t primask)
{
#ifndef SCH_HOST_TEST
    __set_PRIMASK(primask);
#else
    (void)primask;
#endif
}

uint64_t SCH_Now(void)
{
    const uint32_t primask = sch_lock();
    const uint64_t now = g_ticks;
    sch_unlock(primask);
    return now;
}

/* The TIM2 ISR advances elapsed time even while a due task awaits dispatch. */
void SCH_Update(void)
{
    ++g_ticks;
}

static uint32_t sch_find_index(sch_task_id_t id)
{
    uint32_t index = 0U;
    while (index < g_task_count && g_tasks[index].id != id)
    {
        ++index;
    }
    return index;
}

static sch_task_id_t sch_allocate_id(void)
{
    for (uint32_t attempts = 0U; attempts <= SCH_MAX_TASKS; ++attempts)
    {
        const sch_task_id_t candidate = g_next_id++;
        if (g_next_id == 0U)
        {
            g_next_id = 1U;
        }
        if (candidate != g_inflight_id && sch_find_index(candidate) == g_task_count)
        {
            return candidate;
        }
    }
    return 0U;
}

/* Equal absolute deadlines are kept in insertion order. The caller has
 * reserved a slot and captures now once for the entire insertion. */
static void sch_insert(const sch_task_t *task, uint64_t now, uint32_t delay_ticks)
{
    uint64_t remaining = (now - g_queue_tick) + delay_ticks;
    uint32_t index = 0U;
    while (index < g_task_count && remaining >= g_tasks[index].delay)
    {
        remaining -= g_tasks[index].delay;
        ++index;
    }

    if (index < g_task_count)
    {
        g_tasks[index].delay -= remaining;
    }
    for (uint32_t i = g_task_count; i > index; --i)
    {
        g_tasks[i] = g_tasks[i - 1U];
    }
    g_tasks[index] = *task;
    g_tasks[index].delay = remaining;
    ++g_task_count;
}

static void sch_remove(uint32_t index)
{
    /* A deletion preserves all other absolute deadlines. */
    if (index + 1U < g_task_count)
    {
        g_tasks[index + 1U].delay += g_tasks[index].delay;
    }
    for (; index + 1U < g_task_count; ++index)
    {
        g_tasks[index] = g_tasks[index + 1U];
    }
    --g_task_count;
}

void SCH_Init(void)
{
    if (g_dispatching)
    {
        return;
    }

    const uint32_t primask = sch_lock();
    g_ticks = 0U;
    sch_unlock(primask);
    g_queue_tick = 0U;
    g_task_count = 0U;
    g_initialized = true;
}

app_status_t SCH_Add_Task(sch_task_fn_t task,
                          uint32_t delay_ticks,
                          uint32_t period_ticks,
                          sch_task_id_t *id)
{
    if (task == NULL || id == NULL || delay_ticks == 0U)
    {
        return APP_INVALID_ARGUMENT;
    }
    if (!g_initialized)
    {
        return APP_NOT_READY;
    }

    /* Reserve the slot of an in-flight periodic callback until it returns or
     * cancels itself. One-shot callbacks have already freed their slots. */
    const uint32_t reserved = (g_inflight_id != 0U) ? 1U : 0U;
    if (g_task_count + reserved >= SCH_MAX_TASKS)
    {
        return APP_NO_CAPACITY;
    }

    const sch_task_id_t new_id = sch_allocate_id();
    if (new_id == 0U)
    {
        return APP_NO_CAPACITY;
    }

    const sch_task_t new_task = {task, 0U, period_ticks, new_id};
    sch_insert(&new_task, SCH_Now(), delay_ticks);
    *id = new_id;
    return APP_OK;
}

app_status_t SCH_Delete_Task(sch_task_id_t id)
{
    if (id == 0U)
    {
        return APP_INVALID_ARGUMENT;
    }
    if (!g_initialized)
    {
        return APP_NOT_READY;
    }
    if (id == g_inflight_id)
    {
        g_inflight_id = 0U;
        return APP_OK;
    }

    const uint32_t index = sch_find_index(id);
    if (index == g_task_count)
    {
        return APP_NOT_FOUND;
    }
    sch_remove(index);
    return APP_OK;
}

void SCH_Dispatch_Tasks(void)
{
    if (!g_initialized || g_dispatching)
    {
        return;
    }
    g_dispatching = true;

    /* Limit this pass to deadlines due at entry. A callback can add a task or
     * take long enough for a new tick, and the next pass will service it. */
    const uint64_t dispatch_tick = SCH_Now();
    while (g_task_count > 0U && g_tasks[0].delay <= dispatch_tick - g_queue_tick)
    {
        const sch_task_t current = g_tasks[0];
        const uint64_t due_tick = g_queue_tick + current.delay;
        g_queue_tick = due_tick;
        g_tasks[0].delay = 0U;
        sch_remove(0U);

        if (current.period != 0U)
        {
            g_inflight_id = current.id;
        }
        current.function();
        if (g_inflight_id != 0U)
        {
            g_inflight_id = 0U;
            const uint64_t now = SCH_Now();
            /* A delayed callback runs once. Resume at the next deadline on
             * its original period grid rather than replaying missed work. */
            const uint32_t elapsed = (uint32_t)((now - due_tick) % current.period);
            sch_insert(&current, now, current.period - elapsed);
        }
    }

    g_dispatching = false;
}
