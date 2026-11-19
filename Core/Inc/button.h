#ifndef CO3009_BUTTON_H
#define CO3009_BUTTON_H

#include "global.h"

enum {
    BUTTON_ID_1,
    BUTTON_ID_2,
    BUTTON_ID_3,
    BUTTON_ID_4,
    BUTTON_ID_USER,
    BUTTON_ID_COUNT
};

/* Use only with a BUTTON_ID_* input, excluding BUTTON_ID_COUNT. */
#define BUTTON_MASK(id) ((uint8_t)(1U << (id)))
#define BUTTON_MASK_ALL ((uint8_t)((1U << BUTTON_ID_COUNT) - 1U))

/* The edge-only button_event_t in global.h remains available to the FSMs. */
typedef enum {
    BUTTON_EVENT_PRESS = BUTTON_PRESS,
    BUTTON_EVENT_RELEASE = BUTTON_RELEASE,
    BUTTON_EVENT_SHORT,
    BUTTON_EVENT_LONG,
    BUTTON_EVENT_REPEAT
} button_input_event_kind_t;

typedef struct {
    button_id_t id;
    button_input_event_kind_t kind;
} button_input_event_t;

typedef bool (*button_read_level_fn_t)(button_id_t id);
typedef uint32_t (*button_clock_ms_fn_t)(void);

/* Optional override on STM32F103RB with HAL, required on host builds.
 * Bind before init. read_high returns the raw pin level (true = high).
 * Both functions must return promptly. clock_ms is monotonic modulo 2^32. */
app_status_t button_bind_io(button_read_level_fn_t read_high, button_clock_ms_fn_t clock_ms);
/* Before init, supply the measured polarity: set bits press low, clear
 * bits press high. No hardware defaults are assumed. */
app_status_t button_configure_active_low(uint8_t active_low_mask);
/* After GPIO init and polarity configuration. STM32 HAL builds default to
 * the .ioc GPIO inputs and HAL_GetTick; APP_NOT_READY if I/O is missing. */
app_status_t button_init(void);
/* Schedule every 10 ms in the foreground. Uses the bound millisecond clock.
 * Hold times start at the debounced press and end at the debounced release. */
void button_task(void);
/* Only fsm_mode should call this. Pops one event in FIFO order. If its
 * 32 slots fill, the queue drops new events until space is available. */
bool button_take_event(button_input_event_t *event);
/* Drop queued events for the selected IDs and suppress their held presses
 * until three released samples. Leaves debounce and other IDs intact. */
void button_flush(uint8_t button_mask);

#endif /* CO3009_BUTTON_H */
