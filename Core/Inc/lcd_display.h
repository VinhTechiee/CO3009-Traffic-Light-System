#ifndef CO3009_LCD_DISPLAY_H
#define CO3009_LCD_DISPLAY_H

#include "global.h"

typedef struct {
    uint8_t mode_row;
    uint8_t timing_row;
    uint8_t sensor_row;
} lcd_display_layout_t;

/* Call after lcd_init. Timing and sensor may share a row, with timing
 * taking priority during Tuning. Mode has a dedicated row. */
app_status_t lcd_display_configure(const lcd_display_layout_t *layout);
/* Schedule separately from lcd_task and dht20_task in the foreground. */
void lcd_display_task(void);

#endif /* CO3009_LCD_DISPLAY_H */
