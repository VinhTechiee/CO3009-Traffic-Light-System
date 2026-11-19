#ifndef CO3009_TRAFFIC_LIGHT_H
#define CO3009_TRAFFIC_LIGHT_H

#include "global.h"

typedef void (*traffic_light_write_gpio_b_fn_t)(uint16_t pins, bool high);

/* Optional override on STM32F103RB with HAL, required on host builds.
 * Bind before init. Write the selected GPIOB pins synchronously and leave
 * other pins untouched. Bind/init/apply/off are foreground-only. */
app_status_t traffic_light_bind_io(traffic_light_write_gpio_b_fn_t write_gpio_b);
/* After CubeMX GPIO init, establish red on both roads. STM32 HAL builds
 * use HAL_GPIO_WritePin by default. */
app_status_t traffic_light_init(void);
/* Whole pair: reject invalid colors/dual green without writes.
 * Physical write ordering must also prevent transient dual green. */
app_status_t traffic_light_apply(const traffic_state_t *state);
/* Output 00 on both roads, e.g. the dark step of a yellow blink. */
app_status_t traffic_light_off(void);

#endif /* CO3009_TRAFFIC_LIGHT_H */
