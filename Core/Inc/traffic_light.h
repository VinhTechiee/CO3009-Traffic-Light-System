#ifndef CO3009_TRAFFIC_LIGHT_H
#define CO3009_TRAFFIC_LIGHT_H

#include "global.h"

typedef void (*traffic_light_write_gpio_b_fn_t)(uint16_t pins, bool high);

/* Optional override on STM32F103RB with HAL, required on host builds.
 * Bind before init. Write the selected GPIOB pins synchronously and leave
 * other pins untouched. Bind/init/apply/off are foreground-only. */
app_status_t traffic_light_bind_io(traffic_light_write_gpio_b_fn_t write_gpio_b);
/* Establish red on both roads. On STM32 call board_profile_prepare_traffic_gpio
 * first. Without an enabled board profile, use an explicitly bound writer. */
app_status_t traffic_light_init(void);
/* Whole pair: reject invalid colors/dual green without writes.
 * Physical write ordering must also prevent transient dual green. */
app_status_t traffic_light_apply(const traffic_state_t *state);
/* Apply each decoder's configured off code, e.g. for a dark blink step. */
app_status_t traffic_light_off(void);

#endif /* CO3009_TRAFFIC_LIGHT_H */
