#include "board_profile.h"

#include "button.h"
#include "main.h"

#include <stddef.h>

_Static_assert(CO3009_BUTTON_ACTIVE_LOW_MASK == CO3009_BUTTON_MASK_UNVERIFIED
    || (unsigned)CO3009_BUTTON_ACTIVE_LOW_MASK <= BUTTON_MASK_ALL, "Invalid button polarity mask");
_Static_assert(CO3009_TRAFFIC_PROFILE_ENABLED == 0 || CO3009_TRAFFIC_PROFILE_ENABLED == 1,
               "Invalid traffic profile enable flag");
_Static_assert(CO3009_LCD_PCF8574_ENABLED == 0 || CO3009_LCD_PCF8574_ENABLED == 1,
               "Invalid LCD profile enable flag");

void board_profile_prepare_traffic_gpio(void)
{
#if CO3009_TRAFFIC_PROFILE_ENABLED
    const uint16_t pins = TL1_A_Pin | TL1_B_Pin | TL2_A_Pin | TL2_B_Pin;
    const uint16_t high = (uint16_t)(
        board_profile_traffic_high_pins(CO3009_TRAFFIC_ROAD1_RED_CODE, TL1_A_Pin, TL1_B_Pin)
        | board_profile_traffic_high_pins(CO3009_TRAFFIC_ROAD2_RED_CODE, TL2_A_Pin, TL2_B_Pin));

    /* Preload all-red in the GPIO latch while the lines are still high impedance. */
    GPIOB->BSRR = ((uint32_t)(pins ^ high) << 16U) | high;
    GPIO_InitTypeDef init = {
        .Pin = TL1_A_Pin | TL1_B_Pin,
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW
    };
    /* HAL switches pin modes individually. Keep the other road at its
     * verified reset state until both pins on this road are driving red. */
    HAL_GPIO_Init(GPIOB, &init);
    init.Pin = TL2_A_Pin | TL2_B_Pin;
    HAL_GPIO_Init(GPIOB, &init);
#endif
}

const lcd_pcf8574_profile_t *board_profile_lcd(void)
{
#if CO3009_LCD_PCF8574_ENABLED
    static const lcd_pcf8574_profile_t profile = {
        .rows = CO3009_LCD_ROWS,
        .columns = CO3009_LCD_COLUMNS,
        .address_7bit = CO3009_LCD_ADDRESS_7BIT,
        .rs_mask = CO3009_LCD_RS_MASK,
        .enable_mask = CO3009_LCD_ENABLE_MASK,
        .data_masks = {CO3009_LCD_D4_MASK, CO3009_LCD_D5_MASK,
                       CO3009_LCD_D6_MASK, CO3009_LCD_D7_MASK},
        .backlight_mask = CO3009_LCD_BACKLIGHT_MASK,
        .backlight_active_high = CO3009_LCD_BACKLIGHT_ACTIVE_HIGH,
        .ddram_rows = {
            CO3009_LCD_ROW1_DDRAM, CO3009_LCD_ROW2_DDRAM,
#if CO3009_LCD_ROWS > 2
            CO3009_LCD_ROW3_DDRAM,
#endif
#if CO3009_LCD_ROWS > 3
            CO3009_LCD_ROW4_DDRAM,
#endif
        }
    };
    return &profile;
#else
    return NULL;
#endif
}
