#ifndef CO3009_LCD_H
#define CO3009_LCD_H

#include "global.h"

app_status_t lcd_init(void);
/* Copy text to bounded storage; dimensions/encoding await agreement. */
app_status_t lcd_request_line(uint8_t row, const char *text);
/* Scheduled bounded transfer step; no blocking waits. */
void lcd_task(void);

#endif /* CO3009_LCD_H */
