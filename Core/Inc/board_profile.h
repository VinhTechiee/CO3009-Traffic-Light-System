#ifndef CO3009_BOARD_PROFILE_H
#define CO3009_BOARD_PROFILE_H

#include <stdint.h>

/* Buttons use external bias. Set one bit per measured active-low input in
 * BTN1, BTN2, BTN3, BTN4, USER order. The unverified profile ignores inputs. */
#define CO3009_BUTTON_MASK_UNVERIFIED 0xFFU
#ifndef CO3009_BUTTON_ACTIVE_LOW_MASK
#define CO3009_BUTTON_ACTIVE_LOW_MASK CO3009_BUTTON_MASK_UNVERIFIED
#endif

/* The controller board pin assignments are in main.h and the .ioc. Enable
 * push-pull traffic outputs only after verifying both decoders and reset bias.
 * Values are two-bit electrical levels with A as bit 1 and B as bit 0.
 * The defaults preserve the previous firmware's unverified truth table. */
#ifndef CO3009_TRAFFIC_PROFILE_ENABLED
#define CO3009_TRAFFIC_PROFILE_ENABLED 0
#endif
#ifndef CO3009_TRAFFIC_ROAD1_OFF_CODE
#define CO3009_TRAFFIC_ROAD1_OFF_CODE 0U
#endif
#ifndef CO3009_TRAFFIC_ROAD1_RED_CODE
#define CO3009_TRAFFIC_ROAD1_RED_CODE 1U
#endif
#ifndef CO3009_TRAFFIC_ROAD1_YELLOW_CODE
#define CO3009_TRAFFIC_ROAD1_YELLOW_CODE 2U
#endif
#ifndef CO3009_TRAFFIC_ROAD1_GREEN_CODE
#define CO3009_TRAFFIC_ROAD1_GREEN_CODE 3U
#endif
#ifndef CO3009_TRAFFIC_ROAD2_OFF_CODE
#define CO3009_TRAFFIC_ROAD2_OFF_CODE 0U
#endif
#ifndef CO3009_TRAFFIC_ROAD2_RED_CODE
#define CO3009_TRAFFIC_ROAD2_RED_CODE 1U
#endif
#ifndef CO3009_TRAFFIC_ROAD2_YELLOW_CODE
#define CO3009_TRAFFIC_ROAD2_YELLOW_CODE 2U
#endif
#ifndef CO3009_TRAFFIC_ROAD2_GREEN_CODE
#define CO3009_TRAFFIC_ROAD2_GREEN_CODE 3U
#endif

#define CO3009_TRAFFIC_CODES_VALID(a, b, c, d) \
    (((a) | (b) | (c) | (d)) == 3U && (a) != (b) && (a) != (c) \
     && (a) != (d) && (b) != (c) && (b) != (d) && (c) != (d))
#if !CO3009_TRAFFIC_CODES_VALID(CO3009_TRAFFIC_ROAD1_OFF_CODE, CO3009_TRAFFIC_ROAD1_RED_CODE, \
                                CO3009_TRAFFIC_ROAD1_YELLOW_CODE, CO3009_TRAFFIC_ROAD1_GREEN_CODE) \
    || !CO3009_TRAFFIC_CODES_VALID(CO3009_TRAFFIC_ROAD2_OFF_CODE, CO3009_TRAFFIC_ROAD2_RED_CODE, \
                                 CO3009_TRAFFIC_ROAD2_YELLOW_CODE, CO3009_TRAFFIC_ROAD2_GREEN_CODE)
#error Each traffic decoder needs four distinct codes from 0 through 3
#endif
#undef CO3009_TRAFFIC_CODES_VALID

static inline uint16_t board_profile_traffic_high_pins(uint8_t code, uint16_t pin_a,
                                                       uint16_t pin_b)
{
    return (uint16_t)(((code & 2U) != 0U ? pin_a : 0U) | ((code & 1U) != 0U ? pin_b : 0U));
}

#if defined(STM32F103xB) && defined(USE_HAL_DRIVER)
#include "lcd_pcf8574.h"

#ifndef CO3009_LCD_PCF8574_ENABLED
#define CO3009_LCD_PCF8574_ENABLED 0
#endif

/* Call once after GPIO clocks, input setup and the SWJ remap. */
void board_profile_prepare_traffic_gpio(void);
/* NULL until the HD44780-compatible PCF8574 profile has been configured. */
const lcd_pcf8574_profile_t *board_profile_lcd(void);
#endif

#endif /* CO3009_BOARD_PROFILE_H */
