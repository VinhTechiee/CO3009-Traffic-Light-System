#ifndef CO3009_BOARD_DIAGNOSTIC_H
#define CO3009_BOARD_DIAGNOSTIC_H

#include "global.h"
#include "stm32f1xx_hal.h"

/* Foreground boot only, with the traffic GPIOs left as inputs. */
app_status_t board_diagnostic_start(I2C_HandleTypeDef *i2c);

#endif
