#include "button.h"

#include <stddef.h>

#if defined(STM32F103xB) && defined(USE_HAL_DRIVER)
#include "main.h"

/* GPIO inputs configured in the .ioc. Read levels before applying polarity. */
static bool button_read_stm32_high(button_id_t id)
{
    switch (id) {
        case BUTTON_ID_1:  /* D7 */
            return HAL_GPIO_ReadPin(BTN_1_GPIO_Port, BTN_1_Pin) == GPIO_PIN_SET;
        case BUTTON_ID_2:  /* D8 */
            return HAL_GPIO_ReadPin(BTN_2_GPIO_Port, BTN_2_Pin) == GPIO_PIN_SET;
        case BUTTON_ID_3:  /* D9 */
            return HAL_GPIO_ReadPin(BTN_3_GPIO_Port, BTN_3_Pin) == GPIO_PIN_SET;
        case BUTTON_ID_4:  /* D10 */
            return HAL_GPIO_ReadPin(BTN_4_GPIO_Port, BTN_4_Pin) == GPIO_PIN_SET;
        case BUTTON_ID_USER:
            return HAL_GPIO_ReadPin(BTN_USER_GPIO_Port, BTN_USER_Pin) == GPIO_PIN_SET;
        default:
            return false;
    }
}

static button_read_level_fn_t button_read_high = button_read_stm32_high;
static button_clock_ms_fn_t button_clock_ms = HAL_GetTick;
#else
static button_read_level_fn_t button_read_high;
static button_clock_ms_fn_t button_clock_ms;
#endif

enum {
    BUTTON_SAMPLE_MS = 10,
    BUTTON_DEBOUNCE_SAMPLES = 3,
    BUTTON_LONG_MS = 500,
    BUTTON_USER_LONG_MS = 1500,
    BUTTON_FIRST_REPEAT_MS = 500,
    BUTTON_REPEAT_MS = 250,
    BUTTON_QUEUE_CAPACITY = 32
};

#define BUTTON_ADJUST_MASK (BUTTON_MASK(BUTTON_ID_3) | BUTTON_MASK(BUTTON_ID_4))

#ifndef CO3009_BUTTON_CONNECTED_MASK
#define CO3009_BUTTON_CONNECTED_MASK BUTTON_MASK_ALL
#endif

/* Ba mẫu liên tiếp và trạng thái đã chống dội của từng nút. */
static bool button_sample1[BUTTON_ID_COUNT];
static bool button_sample2[BUTTON_ID_COUNT];
static bool button_sample3[BUTTON_ID_COUNT];
static bool button_buffer[BUTTON_ID_COUNT];

static uint32_t button_hold_counter_ms[BUTTON_ID_COUNT];
static uint8_t button_release_samples_left[BUTTON_ID_COUNT];
static uint8_t button_active_low_mask;
static uint32_t button_last_sample_ms;
static bool button_adjust_locked;
static enum { BUTTON_UNCONFIGURED, BUTTON_CONFIGURED, BUTTON_READY } button_status;

static button_input_event_t button_events[BUTTON_QUEUE_CAPACITY];
static uint8_t button_event_count;

static bool button_read_pressed(button_id_t id)
{
    const uint8_t mask = BUTTON_MASK(id);
    const bool active_low = (button_active_low_mask & mask) != 0U;

    return (CO3009_BUTTON_CONNECTED_MASK & mask) != 0U
        && button_read_high(id) != active_low;
}

static void button_push_event(button_id_t id, button_input_event_kind_t kind)
{
    if (button_event_count < BUTTON_QUEUE_CAPACITY) {
        button_events[button_event_count] = (button_input_event_t){id, kind};
        ++button_event_count;
    }
}

static void button_discard_events(uint8_t mask)
{
    uint8_t kept = 0U;

    for (uint8_t i = 0U; i < button_event_count; ++i) {
        if ((mask & BUTTON_MASK(button_events[i].id)) == 0U) {
            button_events[kept] = button_events[i];
            ++kept;
        }
    }
    button_event_count = kept;
}

static bool button_samples_agree(button_id_t id)
{
    return button_sample1[id] == button_sample2[id]
        && button_sample2[id] == button_sample3[id];
}

static uint8_t button_read_samples(void)
{
    uint8_t changed = 0U;

    for (button_id_t id = 0U; id < BUTTON_ID_COUNT; ++id) {
        button_sample3[id] = button_sample2[id];
        button_sample2[id] = button_sample1[id];
        button_sample1[id] = button_read_pressed(id);

        if (button_samples_agree(id) && button_buffer[id] != button_sample1[id]) {
            button_buffer[id] = button_sample1[id];
            changed |= BUTTON_MASK(id);
        }
    }
    return changed;
}

static void button_start_hold(button_id_t id)
{
    switch (id) {
        case BUTTON_ID_2:
            button_hold_counter_ms[id] = BUTTON_LONG_MS;
            break;
        case BUTTON_ID_3:
        case BUTTON_ID_4:
            button_hold_counter_ms[id] = BUTTON_FIRST_REPEAT_MS;
            break;
        case BUTTON_ID_USER:
            button_hold_counter_ms[id] = BUTTON_USER_LONG_MS;
            break;
        default:
            button_hold_counter_ms[id] = 0U;
            break;
    }
}

static void button_count_hold(button_id_t id, uint32_t elapsed_ms)
{
    if (button_hold_counter_ms[id] == 0U) {
        return;
    }
    if (elapsed_ms < button_hold_counter_ms[id]) {
        button_hold_counter_ms[id] -= elapsed_ms;
        return;
    }

    button_hold_counter_ms[id] = 0U;
    if (id == BUTTON_ID_2 || id == BUTTON_ID_USER) {
        button_push_event(id, BUTTON_EVENT_LONG);
    } else if (button_buffer[id]) {
        const button_id_t other = id == BUTTON_ID_3 ? BUTTON_ID_4 : BUTTON_ID_3;

        /* Nạp lại một chu kỳ, kể cả khi task chạy trễ. */
        button_hold_counter_ms[id] = BUTTON_REPEAT_MS;
        if (button_sample1[id] && !button_sample1[other]) {
            button_push_event(id, BUTTON_EVENT_REPEAT);
        }
    }
}

static void button_process_input(button_id_t id, bool changed, uint32_t elapsed_ms)
{
    if (changed && button_buffer[id]) {
        button_start_hold(id);
        button_push_event(id, BUTTON_EVENT_PRESS);
        return;
    }

    /* Thả đúng ngưỡng vẫn tính là LONG. */
    button_count_hold(id, elapsed_ms);
    if (changed) {
        button_push_event(id, BUTTON_EVENT_RELEASE);
        if ((id == BUTTON_ID_2 || id == BUTTON_ID_USER) && button_hold_counter_ms[id] != 0U) {
            button_push_event(id, BUTTON_EVENT_SHORT);
        }
        button_hold_counter_ms[id] = 0U;
    }
}

app_status_t button_bind_io(button_read_level_fn_t read_high, button_clock_ms_fn_t clock_ms)
{
    if (read_high == NULL || clock_ms == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (button_status == BUTTON_READY) {
        return APP_BUSY;
    }

    button_read_high = read_high;
    button_clock_ms = clock_ms;
    return APP_OK;
}

app_status_t button_configure_active_low(uint8_t active_low_mask)
{
    if ((active_low_mask & (uint8_t)~BUTTON_MASK_ALL) != 0U) {
        return APP_INVALID_ARGUMENT;
    }
    if (button_status == BUTTON_READY) {
        return APP_BUSY;
    }

    button_active_low_mask = active_low_mask;
    button_status = BUTTON_CONFIGURED;
    return APP_OK;
}

app_status_t button_init(void)
{
    if (button_status == BUTTON_UNCONFIGURED || button_read_high == NULL || button_clock_ms == NULL) {
        return APP_NOT_READY;
    }
    if (button_status == BUTTON_READY) {
        return APP_BUSY;
    }

    button_last_sample_ms = button_clock_ms() - BUTTON_SAMPLE_MS;
    button_status = BUTTON_READY;
    return APP_OK;
}

void button_task(void)
{
    if (button_status != BUTTON_READY) {
        return;
    }

    const uint32_t now_ms = button_clock_ms();
    const uint32_t elapsed_ms = now_ms - button_last_sample_ms;

    if (elapsed_ms < BUTTON_SAMPLE_MS) {
        return;
    }
    button_last_sample_ms = now_ms;

    /* Đọc đủ cả năm nút trước khi xử lý cặp BTN3/BTN4. */
    const uint8_t changed = button_read_samples();

    if (!button_adjust_locked && button_buffer[BUTTON_ID_3] && button_buffer[BUTTON_ID_4]) {
        button_adjust_locked = true;
        button_hold_counter_ms[BUTTON_ID_3] = 0U;
        button_hold_counter_ms[BUTTON_ID_4] = 0U;
        button_discard_events(BUTTON_ADJUST_MASK);
    }

    for (button_id_t id = 0U; id < BUTTON_ID_COUNT; ++id) {
        if (button_release_samples_left[id] != 0U) {
            if (button_sample1[id]) {
                button_release_samples_left[id] = BUTTON_DEBOUNCE_SAMPLES;
            } else {
                --button_release_samples_left[id];
            }
        } else if (!button_adjust_locked || (BUTTON_ADJUST_MASK & BUTTON_MASK(id)) == 0U) {
            button_process_input(id, (changed & BUTTON_MASK(id)) != 0U, elapsed_ms);
        }
    }

    if (button_adjust_locked
        && !button_sample1[BUTTON_ID_3] && button_samples_agree(BUTTON_ID_3)
        && !button_sample1[BUTTON_ID_4] && button_samples_agree(BUTTON_ID_4)) {
        button_adjust_locked = false;
    }
}

bool button_take_event(button_input_event_t *event)
{
    if (event == NULL || button_event_count == 0U) {
        return false;
    }

    *event = button_events[0];
    --button_event_count;
    for (uint8_t i = 0U; i < button_event_count; ++i) {
        button_events[i] = button_events[i + 1U];
    }
    return true;
}

void button_flush(uint8_t button_mask)
{
    if (button_status != BUTTON_READY) {
        return;
    }

    button_discard_events(button_mask);
    for (button_id_t id = 0U; id < BUTTON_ID_COUNT; ++id) {
        if ((button_mask & BUTTON_MASK(id)) != 0U
            && (button_buffer[id] || button_sample1[id] || button_read_pressed(id))) {
            button_release_samples_left[id] = BUTTON_DEBOUNCE_SAMPLES;
            button_hold_counter_ms[id] = 0U;
        }
    }
}
