#ifndef CO3009_LCD_PRESENTATION_H
#define CO3009_LCD_PRESENTATION_H

#include <stddef.h>
#include "global.h"

/* Read DHT20 and format one sensor line. Call serially in foreground, under
 * the DHT20 driver's non-reentrancy contract. Width counts visible characters;
 * capacity includes the NUL. Submit only APP_OK output as a complete line.
 * APP_NO_CAPACITY leaves a terminated prefix if capacity is nonzero.
 * Getter failures, including APP_NOT_READY before configuration, and invalid
 * arguments leave output unchanged. With usable metadata, fresh, stale,
 * never-sampled and unavailable states can all produce complete lines.
 */
app_status_t lcd_presentation_sensor_line(char *output, size_t capacity, size_t width);

#endif /* CO3009_LCD_PRESENTATION_H */
