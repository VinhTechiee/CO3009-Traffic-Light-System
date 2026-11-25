#ifndef CO3009_I2C_STM32_H
#define CO3009_I2C_STM32_H

#include "global.h"
#include "i2c_async.h"
#include "stm32f1xx_hal.h"

#define I2C_STM32_MAX_TRANSFER_BYTES 48U

typedef enum {
    I2C_STM32_TX_COMPLETE, I2C_STM32_RX_COMPLETE,
    I2C_STM32_ERROR, I2C_STM32_ABORT_COMPLETE
} i2c_stm32_event_t;

/* Caller-owned storage for one exclusive I2C1 master, with no DMA. */
typedef struct {
    I2C_HandleTypeDef *hal;
    const void *owner;
    const void *waiter;
    i2c_async_token_t token;
    uint32_t waiter_ms;
    uint32_t busy_ms;
    uint32_t cancel_ms;
    uint8_t *receive_destination;
    uint16_t length;
    volatile uint8_t state;
    bool busy_observed;
    bool ready;
    /* HAL error/abort handlers can consume one residual DR byte. */
    uint8_t staging[I2C_STM32_MAX_TRANSFER_BYTES + 1U];
} i2c_stm32_t;

/* Initialize zeroed backend storage after HAL_I2C_Init. Keep backend and
 * handle alive while the bus is in use. Foreground owns submit/poll/cancel. */
app_status_t i2c_stm32_init(i2c_stm32_t *backend, I2C_HandleTypeDef *hal,
                            i2c_async_t *bus);

/* HAL callbacks report completion only. Foreground retires the transaction. */
void i2c_stm32_notify(i2c_stm32_t *backend, const I2C_HandleTypeDef *hal,
                       i2c_stm32_event_t event);

#endif /* CO3009_I2C_STM32_H */
