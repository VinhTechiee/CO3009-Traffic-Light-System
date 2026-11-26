#ifndef CO3009_LCD_FORMAT_H
#define CO3009_LCD_FORMAT_H

#include <stddef.h>
#include "dht20.h"
#include "fsm_automatic.h"

/* width is the maximum visible characters; capacity includes the terminator.
 * APP_OK means the entire line fits. APP_NO_CAPACITY means a terminated prefix
 * was written (if capacity > 0). Submit only APP_OK output as a complete display
 * line. Invalid arguments leave output unchanged.
 * A zero width is invalid. Lines are unpadded ASCII.
 */
/* info is required. sample may be NULL when no reading is available.
 * When supplied, sample and info must come from the same DHT20 foreground
 * observation.
 */
app_status_t lcd_format_sensor(char *output, size_t capacity, size_t width,
                               const dht20_sample_t *sample,
                               const dht20_info_t *info);
app_status_t lcd_format_mode(char *output, size_t capacity, size_t width,
                             app_mode_t mode);
/* Format the Auto mode and both countdowns on one line. */
app_status_t lcd_format_countdown(char *output, size_t capacity, size_t width,
                                  const fsm_automatic_view_t *view);
/* Format the currently applied Manual colors. */
app_status_t lcd_format_manual(char *output, size_t capacity, size_t width,
                               const traffic_state_t *phase);
/* Display red and the green or yellow duration selected for adjustment. */
app_status_t lcd_format_timing(char *output, size_t capacity, size_t width,
                               const timing_config_t *config, traffic_color_t color);

#endif /* CO3009_LCD_FORMAT_H */
