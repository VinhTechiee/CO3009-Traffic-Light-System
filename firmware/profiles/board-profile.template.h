#ifndef CO3009_MEASURED_BOARD_PROFILE_H
#define CO3009_MEASURED_BOARD_PROFILE_H

/*
 * Copy to measured-board-profile.h and fill from the actual board.
 * Record board/revision, date, operator, the measured logic and pull-up
 * voltages, and the results of the reset/high-impedance checks with this file.
 * The template keeps uncalibrated buttons, traffic outputs, and LCD disabled.
 */

/* Replace the 0xFF sentinel with the measured mask. Bits 0, 1, 2, 3 and 4
 * are BTN1, BTN2, BTN3, BTN4 and USER. Set a bit only for idle-high/pressed-low. */
#define CO3009_BUTTON_ACTIVE_LOW_MASK 0xFFU

/* Set to 1 after independently checking both decoder truth tables and the
 * reset/high-impedance bias of both roads. For each code, A is bit 1 and B
 * is bit 0. Use four distinct measured codes (0 through 3) per road. */
#define CO3009_TRAFFIC_PROFILE_ENABLED 0
#if CO3009_TRAFFIC_PROFILE_ENABLED
#define CO3009_TRAFFIC_ROAD1_OFF_CODE    MEASURED_ROAD1_OFF
#define CO3009_TRAFFIC_ROAD1_RED_CODE    MEASURED_ROAD1_RED
#define CO3009_TRAFFIC_ROAD1_YELLOW_CODE MEASURED_ROAD1_YELLOW
#define CO3009_TRAFFIC_ROAD1_GREEN_CODE  MEASURED_ROAD1_GREEN
#define CO3009_TRAFFIC_ROAD2_OFF_CODE    MEASURED_ROAD2_OFF
#define CO3009_TRAFFIC_ROAD2_RED_CODE    MEASURED_ROAD2_RED
#define CO3009_TRAFFIC_ROAD2_YELLOW_CODE MEASURED_ROAD2_YELLOW
#define CO3009_TRAFFIC_ROAD2_GREEN_CODE  MEASURED_ROAD2_GREEN
#endif

/* Set to 1 for a verified HD44780-compatible four-bit LCD with a PCF8574/A
 * expander. RW must be tied low or on an otherwise unused expander pin.
 * Verify pull-up voltages and level shifting. DHT20's 7-bit address is 0x38.
 * Measure the LCD's separate 7-bit address and identify the expander wiring.
 * The application needs at least two text rows. This adapter accepts LCD
 * addresses 0x20 through 0x27 or 0x39 through 0x3F. */
#define CO3009_LCD_PCF8574_ENABLED 0
#if CO3009_LCD_PCF8574_ENABLED
#define CO3009_LCD_ROWS                  MEASURED_LCD_ROWS
#define CO3009_LCD_COLUMNS               MEASURED_LCD_COLUMNS
#define CO3009_LCD_ADDRESS_7BIT          MEASURED_LCD_ADDRESS
/* Each mask below is 1 shifted left by the connected expander pin index. */
#define CO3009_LCD_RS_MASK               MEASURED_LCD_RS_MASK
#define CO3009_LCD_ENABLE_MASK           MEASURED_LCD_ENABLE_MASK
#define CO3009_LCD_D4_MASK               MEASURED_LCD_D4_MASK
#define CO3009_LCD_D5_MASK               MEASURED_LCD_D5_MASK
#define CO3009_LCD_D6_MASK               MEASURED_LCD_D6_MASK
#define CO3009_LCD_D7_MASK               MEASURED_LCD_D7_MASK
/* Use a mask of 0 for externally controlled backlight. Otherwise set the
 * mask to its expander pin and polarity to 1 for active high or 0 for active low. */
#define CO3009_LCD_BACKLIGHT_MASK        MEASURED_LCD_BACKLIGHT_MASK
#define CO3009_LCD_BACKLIGHT_ACTIVE_HIGH MEASURED_LCD_BACKLIGHT_ACTIVE_HIGH
/* Raw DDRAM row start addresses from the actual display geometry. */
#define CO3009_LCD_ROW1_DDRAM            MEASURED_ROW1_START
#define CO3009_LCD_ROW2_DDRAM            MEASURED_ROW2_START
#define CO3009_LCD_ROW3_DDRAM            MEASURED_ROW3_START /* If present */
#define CO3009_LCD_ROW4_DDRAM            MEASURED_ROW4_START /* If present */
#endif

#endif /* CO3009_MEASURED_BOARD_PROFILE_H */
