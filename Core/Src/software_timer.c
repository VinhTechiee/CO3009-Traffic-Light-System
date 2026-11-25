#include "software_timer.h"

#include <stddef.h>
#include <stdint.h>

#ifndef SOFTWARE_TIMER_MAX_COUNT
#define SOFTWARE_TIMER_MAX_COUNT 8U
#endif

typedef struct
{
    software_timer_id_t id;
    uint32_t remaining_ticks;
    uint8_t used;
    uint8_t expired;
} software_timer_entry_t;

static software_timer_entry_t g_timers[SOFTWARE_TIMER_MAX_COUNT];
static software_timer_id_t g_next_timer_id = 1U;
static uint8_t g_initialized;
static software_timer_clock_fn_t g_clock_ticks;
static uint64_t g_last_tick;

static uint8_t timer_id_in_use(software_timer_id_t id)
{
    uint32_t i;

    for (i = 0U; i < SOFTWARE_TIMER_MAX_COUNT; ++i)
    {
        if ((g_timers[i].used != 0U) && (g_timers[i].id == id))
        {
            return 1U;
        }
    }
    return 0U;
}

static software_timer_id_t timer_allocate_id(void)
{
    uint32_t attempts;

    for (attempts = 0U; attempts <= SOFTWARE_TIMER_MAX_COUNT; ++attempts)
    {
        software_timer_id_t candidate = g_next_timer_id++;

        if (g_next_timer_id == 0U)
        {
            g_next_timer_id = 1U;
        }
        if (candidate == 0U)
        {
            candidate = g_next_timer_id++;
            if (g_next_timer_id == 0U)
            {
                g_next_timer_id = 1U;
            }
        }
        if ((candidate != 0U) && (timer_id_in_use(candidate) == 0U))
        {
            return candidate;
        }
    }

    return 0U;
}

void software_timer_init(void)
{
    uint32_t i;

    for (i = 0U; i < SOFTWARE_TIMER_MAX_COUNT; ++i)
    {
        g_timers[i].id = 0U;
        g_timers[i].remaining_ticks = 0U;
        g_timers[i].used = 0U;
        g_timers[i].expired = 0U;
    }

    g_clock_ticks = NULL;
    g_initialized = 1U;
}

app_status_t software_timer_init_with_clock(software_timer_clock_fn_t clock_ticks)
{
    if (clock_ticks == NULL)
    {
        return APP_INVALID_ARGUMENT;
    }

    software_timer_init();
    g_clock_ticks = clock_ticks;
    g_last_tick = clock_ticks();
    return APP_OK;
}

static void software_timer_advance(uint64_t elapsed_ticks)
{
    if (elapsed_ticks == 0U)
    {
        return;
    }
    for (uint32_t i = 0U; i < SOFTWARE_TIMER_MAX_COUNT; ++i)
    {
        if (g_timers[i].used != 0U && g_timers[i].expired == 0U)
        {
            if (elapsed_ticks < g_timers[i].remaining_ticks)
            {
                g_timers[i].remaining_ticks -= (uint32_t)elapsed_ticks;
            }
            else
            {
                g_timers[i].remaining_ticks = 0U;
                g_timers[i].expired = 1U;
            }
        }
    }
}

static void software_timer_sample_clock(void)
{
    if (g_clock_ticks != NULL)
    {
        const uint64_t now = g_clock_ticks();
        software_timer_advance(now - g_last_tick);
        g_last_tick = now;
    }
}

app_status_t software_timer_start(uint32_t duration_ticks,
                                  software_timer_id_t *id)
{
    uint32_t i;
    software_timer_id_t new_id;

    if ((duration_ticks == 0U) || (id == NULL))
    {
        return APP_INVALID_ARGUMENT;
    }
    if (g_initialized == 0U)
    {
        return APP_NOT_READY;
    }

    for (i = 0U; i < SOFTWARE_TIMER_MAX_COUNT; ++i)
    {
        if (g_timers[i].used == 0U)
        {
            break;
        }
    }

    if (i >= SOFTWARE_TIMER_MAX_COUNT)
    {
        return APP_NO_CAPACITY;
    }

    new_id = timer_allocate_id();
    if (new_id == 0U)
    {
        return APP_NO_CAPACITY;
    }

    software_timer_sample_clock();
    g_timers[i].id = new_id;
    g_timers[i].remaining_ticks = duration_ticks;
    g_timers[i].expired = 0U;
    g_timers[i].used = 1U;

    *id = new_id; /* Do not modify output on failure. */
    return APP_OK;
}

app_status_t software_timer_cancel(software_timer_id_t id)
{
    uint32_t i;

    if (g_initialized == 0U)
    {
        return APP_NOT_READY;
    }

    for (i = 0U; i < SOFTWARE_TIMER_MAX_COUNT; ++i)
    {
        if ((g_timers[i].used != 0U) && (g_timers[i].id == id))
        {
            g_timers[i].id = 0U;
            g_timers[i].remaining_ticks = 0U;
            g_timers[i].expired = 0U;
            g_timers[i].used = 0U;
            return APP_OK;
        }
    }

    return APP_NOT_FOUND;
}

app_status_t software_timer_is_expired(software_timer_id_t id, bool *expired)
{
    uint32_t i;

    if (expired == NULL)
    {
        return APP_INVALID_ARGUMENT;
    }
    if (g_initialized == 0U)
    {
        return APP_NOT_READY;
    }

    software_timer_sample_clock();
    *expired = false;
    for (i = 0U; i < SOFTWARE_TIMER_MAX_COUNT; ++i)
    {
        if ((g_timers[i].used != 0U) && (g_timers[i].id == id))
        {
            *expired = (g_timers[i].expired != 0U);
            return APP_OK;
        }
    }

    return APP_NOT_FOUND;
}

void software_timer_tick(void)
{
    if (g_initialized == 0U)
    {
        return;
    }

    if (g_clock_ticks != NULL)
    {
        software_timer_sample_clock();
    }
    else
    {
        software_timer_advance(1U);
    }
}
