#include "traffic_light.h"

#include <stddef.h>

#if defined(STM32F103xB) && defined(USE_HAL_DRIVER)
#include "stm32f1xx_hal.h"

/* GPIOB pin labels in Project_Traffic_Light_System.ioc. */
#define TRAFFIC_LIGHT_TL1_A GPIO_PIN_3   /* D3 */
#define TRAFFIC_LIGHT_TL1_B GPIO_PIN_5   /* D4 */
#define TRAFFIC_LIGHT_TL2_A GPIO_PIN_4   /* D5 */
#define TRAFFIC_LIGHT_TL2_B GPIO_PIN_10  /* D6 */

static void traffic_light_write_stm32_gpio_b(uint16_t pins, bool high)
{
    HAL_GPIO_WritePin(GPIOB, pins, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static traffic_light_write_gpio_b_fn_t traffic_light_write_gpio_b = traffic_light_write_stm32_gpio_b;
#else
/* Same GPIOB masks for tests that run without the STM32 HAL. */
#define TRAFFIC_LIGHT_TL1_A (1U << 3)
#define TRAFFIC_LIGHT_TL1_B (1U << 5)
#define TRAFFIC_LIGHT_TL2_A (1U << 4)
#define TRAFFIC_LIGHT_TL2_B (1U << 10)

static traffic_light_write_gpio_b_fn_t traffic_light_write_gpio_b;
#endif

#define TRAFFIC_LIGHT_PINS (TRAFFIC_LIGHT_TL1_A | TRAFFIC_LIGHT_TL1_B \
                            | TRAFFIC_LIGHT_TL2_A | TRAFFIC_LIGHT_TL2_B)

static bool traffic_light_initialized;

/* A is the first bit: OFF=00, RED=01, YELLOW=10, GREEN=11. */
static bool traffic_light_color_pins(traffic_color_t color, uint16_t pin_a,
                                     uint16_t pin_b, uint16_t *high_pins)
{
    switch (color) {
        case TRAFFIC_RED:
            *high_pins = pin_b;
            return true;
        case TRAFFIC_YELLOW:
            *high_pins = pin_a;
            return true;
        case TRAFFIC_GREEN:
            *high_pins = pin_a | pin_b;
            return true;
        default:
            return false;
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
    if (traffic_light_write_gpio_b == NULL) {
        return APP_NOT_READY;
    }

    traffic_light_write_gpio_b(TRAFFIC_LIGHT_PINS, false);
    traffic_light_write_gpio_b(TRAFFIC_LIGHT_TL1_B | TRAFFIC_LIGHT_TL2_B, true);
    traffic_light_initialized = true;
    return APP_OK;
}

app_status_t traffic_light_apply(const traffic_state_t *state)
{
    uint16_t road_1_high;
    uint16_t road_2_high;

    if (state == NULL || (state->road_1 == TRAFFIC_GREEN && state->road_2 == TRAFFIC_GREEN)
        || !traffic_light_color_pins(state->road_1, TRAFFIC_LIGHT_TL1_A, TRAFFIC_LIGHT_TL1_B,
                                     &road_1_high)
        || !traffic_light_color_pins(state->road_2, TRAFFIC_LIGHT_TL2_A, TRAFFIC_LIGHT_TL2_B,
                                     &road_2_high)) {
        return APP_INVALID_ARGUMENT;
    }
    if (!traffic_light_initialized) {
        return APP_NOT_READY;
    }

    const uint16_t high_pins = road_1_high | road_2_high;
    const uint16_t low_pins = (uint16_t)(TRAFFIC_LIGHT_PINS & ~high_pins);

    /* Clearing first cannot introduce a green. During setting, all high
     * pins are already a subset of the validated target. */
    traffic_light_write_gpio_b(low_pins, false);
    traffic_light_write_gpio_b(high_pins, true);
    return APP_OK;
}

app_status_t traffic_light_off(void)
{
    if (!traffic_light_initialized) {
        return APP_NOT_READY;
    }

    traffic_light_write_gpio_b(TRAFFIC_LIGHT_PINS, false);
    return APP_OK;
}
