#ifndef CO3009_TIMING_RULES_H
#define CO3009_TIMING_RULES_H

#include "global.h"
#include <stddef.h>

#ifndef FSM_AUTOMATIC_TICK_MS
#define FSM_AUTOMATIC_TICK_MS 10U
#endif

#if (FSM_AUTOMATIC_TICK_MS == 0U)
#error "FSM_AUTOMATIC_TICK_MS must be greater than zero"
#endif

static inline uint32_t timing_ticks_ceil(uint32_t milliseconds)
{
    return milliseconds / FSM_AUTOMATIC_TICK_MS
        + (uint32_t)(milliseconds % FSM_AUTOMATIC_TICK_MS != 0U);
}

/* Both the requested and scheduled red intervals must cover the other road's
 * green and yellow intervals. Any extra ticks become all-red clearance. */
static inline bool timing_config_valid(const timing_config_t *config)
{
    return config != NULL && config->green_ms != 0U && config->yellow_ms != 0U
        && (uint64_t)config->red_ms >= (uint64_t)config->green_ms + config->yellow_ms
        && (uint64_t)timing_ticks_ceil(config->red_ms)
           >= (uint64_t)timing_ticks_ceil(config->green_ms)
              + timing_ticks_ceil(config->yellow_ms);
}

#endif /* CO3009_TIMING_RULES_H */
