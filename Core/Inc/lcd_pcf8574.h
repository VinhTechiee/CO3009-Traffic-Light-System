#ifndef CO3009_LCD_PCF8574_H
#define CO3009_LCD_PCF8574_H

#include "lcd.h"

#define LCD_PCF8574_TRANSFER_BYTES 48U

/* An explicitly selected HD44780-compatible 4-bit module with one
 * PCF8574/PCF8574A. RS, E and D4-7 are active-high. RW must be tied low
 * or use an otherwise unused expander pin, which is always driven low. */
typedef struct {
    uint8_t rows;
    uint8_t columns;
    uint8_t address_7bit;
    uint8_t rs_mask;
    uint8_t enable_mask;
    uint8_t data_masks[4];
    uint8_t backlight_mask; /* Zero if absent or controlled externally. */
    bool backlight_active_high;
    uint8_t ddram_rows[LCD_MAX_ROWS];
} lcd_pcf8574_profile_t;

typedef struct {
    lcd_pcf8574_profile_t profile;
    uint32_t (*clock_ms)(void *);
    void *clock_context;
    i2c_async_token_t token;
    uint32_t wait_started_ms;
    uint32_t wait_ms;
    uint32_t transfer_started_ms;
    size_t tx_length;
    uint8_t tx[LCD_PCF8574_TRANSFER_BYTES];
    uint8_t init_step;
    uint8_t column;
    uint8_t next_column;
    bool prepared;
    bool accepted;
    bool cancelling;
} lcd_pcf8574_t;

/* Copy a verified hardware profile into long-lived controller storage and
 * populate the generic LCD configuration. Call once before lcd_init. */
app_status_t lcd_pcf8574_configure(lcd_pcf8574_t *controller, const i2c_async_t *bus,
    const lcd_pcf8574_profile_t *profile, uint32_t (*clock_ms)(void *),
    void *clock_context, lcd_config_t *lcd_config);

#endif /* CO3009_LCD_PCF8574_H */
