#include "traffic_light.h"

#include "board_profile.h"

#include <stddef.h>

#if defined(STM32F103xB) && defined(USE_HAL_DRIVER)
#include "main.h"

#define TRAFFIC_LIGHT_TL1_A TL1_A_Pin
#define TRAFFIC_LIGHT_TL1_B TL1_B_Pin
#define TRAFFIC_LIGHT_TL2_A TL2_A_Pin
#define TRAFFIC_LIGHT_TL2_B TL2_B_Pin
#else
/* Same GPIOB masks for tests that run without the STM32 HAL. */
#define TRAFFIC_LIGHT_TL1_A (1U << 3)
#define TRAFFIC_LIGHT_TL1_B (1U << 5)
#define TRAFFIC_LIGHT_TL2_A (1U << 4)
#define TRAFFIC_LIGHT_TL2_B (1U << 10)
#endif

#define TRAFFIC_LIGHT_PINS (TRAFFIC_LIGHT_TL1_A | TRAFFIC_LIGHT_TL1_B \
                            | TRAFFIC_LIGHT_TL2_A | TRAFFIC_LIGHT_TL2_B)

enum { TRAFFIC_LIGHT_ROAD1, TRAFFIC_LIGHT_ROAD2, TRAFFIC_LIGHT_ROAD_COUNT };

static const struct {
    uint16_t pin_a;
    uint16_t pin_b;
    uint8_t colors[TRAFFIC_GREEN + 1];
    uint8_t off;
} traffic_light_roads[TRAFFIC_LIGHT_ROAD_COUNT] = {
    [TRAFFIC_LIGHT_ROAD1] = {
        .pin_a = TRAFFIC_LIGHT_TL1_A, .pin_b = TRAFFIC_LIGHT_TL1_B,
        .colors = {
            [TRAFFIC_RED] = CO3009_TRAFFIC_ROAD1_RED_CODE,
            [TRAFFIC_YELLOW] = CO3009_TRAFFIC_ROAD1_YELLOW_CODE,
            [TRAFFIC_GREEN] = CO3009_TRAFFIC_ROAD1_GREEN_CODE
        },
        .off = CO3009_TRAFFIC_ROAD1_OFF_CODE
    },
    [TRAFFIC_LIGHT_ROAD2] = {
        .pin_a = TRAFFIC_LIGHT_TL2_A, .pin_b = TRAFFIC_LIGHT_TL2_B,
        .colors = {
            [TRAFFIC_RED] = CO3009_TRAFFIC_ROAD2_RED_CODE,
            [TRAFFIC_YELLOW] = CO3009_TRAFFIC_ROAD2_YELLOW_CODE,
            [TRAFFIC_GREEN] = CO3009_TRAFFIC_ROAD2_GREEN_CODE
        },
        .off = CO3009_TRAFFIC_ROAD2_OFF_CODE
    }
};

static traffic_light_write_gpio_b_fn_t traffic_light_write_gpio_b;
static bool traffic_light_initialized;
static uint8_t traffic_light_current_codes[TRAFFIC_LIGHT_ROAD_COUNT];

static void traffic_light_write_pins(uint16_t pins, uint16_t high)
{
    const uint16_t low = pins ^ high;

    if (traffic_light_write_gpio_b != NULL) {
        if (low != 0U) {
            traffic_light_write_gpio_b(low, false);
        }
        if (high != 0U) {
            traffic_light_write_gpio_b(high, true);
        }
        return;
    }
#if defined(STM32F103xB) && defined(USE_HAL_DRIVER)
    GPIOB->BSRR = ((uint32_t)low << 16U) | high;
#endif
}

static void traffic_light_drive_code(unsigned road, uint8_t code)
{
    const uint16_t pin_a = traffic_light_roads[road].pin_a;
    const uint16_t pin_b = traffic_light_roads[road].pin_b;

    traffic_light_write_pins(pin_a | pin_b, board_profile_traffic_high_pins(code, pin_a, pin_b));
}

static void traffic_light_write_road(unsigned road, uint8_t code)
{
    const uint8_t previous = traffic_light_current_codes[road];
    if (previous == code) {
        return;
    }

    /* Move one signal at a time. If both bits change, at least one of the
     * two possible intermediate codes is non-green. */
    if ((previous ^ code) == 3U) {
        uint8_t through = previous ^ 1U;
        if (through == traffic_light_roads[road].colors[TRAFFIC_GREEN]) {
            through = previous ^ 2U;
        }
        traffic_light_drive_code(road, through);
    }
    traffic_light_drive_code(road, code);
    traffic_light_current_codes[road] = code;
}

static void traffic_light_apply_codes(uint8_t road1_code, uint8_t road2_code)
{
    /* Establish the other road's non-green code before even starting the
     * green destination's move. This also clears an outgoing green first. */
    if (road1_code == traffic_light_roads[TRAFFIC_LIGHT_ROAD1].colors[TRAFFIC_GREEN]) {
        traffic_light_write_road(TRAFFIC_LIGHT_ROAD2, road2_code);
        traffic_light_write_road(TRAFFIC_LIGHT_ROAD1, road1_code);
    } else {
        traffic_light_write_road(TRAFFIC_LIGHT_ROAD1, road1_code);
        traffic_light_write_road(TRAFFIC_LIGHT_ROAD2, road2_code);
    }
}

app_status_t traffic_light_bind_io(traffic_light_write_gpio_b_fn_t write_gpio_b)
{
    if (write_gpio_b == NULL) {
        return APP_INVALID_ARGUMENT;
    }
    if (traffic_light_initialized) {
        return APP_BUSY;
    }

    traffic_light_write_gpio_b = write_gpio_b;
    return APP_OK;
}

app_status_t traffic_light_init(void)
{
#if !defined(STM32F103xB) || !defined(USE_HAL_DRIVER) || !CO3009_TRAFFIC_PROFILE_ENABLED
    if (traffic_light_write_gpio_b == NULL) {
        return APP_NOT_READY;
    }
#endif
    if (traffic_light_initialized) {
        traffic_light_apply_codes(CO3009_TRAFFIC_ROAD1_RED_CODE, CO3009_TRAFFIC_ROAD2_RED_CODE);
        return APP_OK;
    }

    const uint16_t high = (uint16_t)(
        board_profile_traffic_high_pins(CO3009_TRAFFIC_ROAD1_RED_CODE,
                                       TRAFFIC_LIGHT_TL1_A, TRAFFIC_LIGHT_TL1_B)
        | board_profile_traffic_high_pins(CO3009_TRAFFIC_ROAD2_RED_CODE,
                                         TRAFFIC_LIGHT_TL2_A, TRAFFIC_LIGHT_TL2_B));

    traffic_light_write_pins(TRAFFIC_LIGHT_PINS, high);
    traffic_light_current_codes[TRAFFIC_LIGHT_ROAD1] = CO3009_TRAFFIC_ROAD1_RED_CODE;
    traffic_light_current_codes[TRAFFIC_LIGHT_ROAD2] = CO3009_TRAFFIC_ROAD2_RED_CODE;
    traffic_light_initialized = true;
    return APP_OK;
}

app_status_t traffic_light_apply(const traffic_state_t *state)
{
    if (state == NULL || (state->road_1 == TRAFFIC_GREEN && state->road_2 == TRAFFIC_GREEN)
        || (unsigned)state->road_1 > TRAFFIC_GREEN || (unsigned)state->road_2 > TRAFFIC_GREEN) {
        return APP_INVALID_ARGUMENT;
    }
    if (!traffic_light_initialized) {
        return APP_NOT_READY;
    }

    traffic_light_apply_codes(traffic_light_roads[TRAFFIC_LIGHT_ROAD1].colors[state->road_1],
                              traffic_light_roads[TRAFFIC_LIGHT_ROAD2].colors[state->road_2]);
    return APP_OK;
}

app_status_t traffic_light_off(void)
{
    if (!traffic_light_initialized) {
        return APP_NOT_READY;
    }

    traffic_light_apply_codes(traffic_light_roads[TRAFFIC_LIGHT_ROAD1].off,
                              traffic_light_roads[TRAFFIC_LIGHT_ROAD2].off);
    return APP_OK;
}
