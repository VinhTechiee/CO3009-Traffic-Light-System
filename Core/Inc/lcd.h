#ifndef CO3009_LCD_H
#define CO3009_LCD_H

#include "global.h"
#include "i2c_async.h"

/* Storage capacities, independent of the attached display. */
#define LCD_MAX_ROWS 4U
#define LCD_MAX_COLUMNS 40U

typedef struct {
    uint8_t rows;
    uint8_t columns;
    uint8_t address_7bit;
    i2c_async_t bus;
    void *context;
    /* Scheduled controller startup; return APP_OK when initialized. */
    app_status_t (*prepare)(void *context, const i2c_async_t *bus, uint8_t address_7bit);
    /* Repeated with the same padded line and arguments until terminal.
     * APP_BUSY/APP_NOT_READY keeps the operation pending. Other statuses
     * are terminal and release all references to the LCD line buffer.
     * Adapter owns device protocol, deadlines/abort, and a unique bus owner. */
    app_status_t (*write_line)(void *context, const i2c_async_t *bus, uint8_t address_7bit,
                               uint8_t row, const char *text, uint8_t columns);
} lcd_config_t;

/* Foreground-only. Copies config. Backend context lifetime spans LCD use. */
app_status_t lcd_configure(const lcd_config_t *config);
app_status_t lcd_init(void);
app_status_t lcd_get_size(uint8_t *rows, uint8_t *columns);
/* Copy and pad text. Overlong text returns APP_NO_CAPACITY, leaving line intact. */
app_status_t lcd_request_line(uint8_t row, const char *text);
/* At most one nonblocking backend step per invocation. */
void lcd_task(void);

#endif /* CO3009_LCD_H */
