#include "i2c_stm32.h"

#include <string.h>

enum {
    I2C_STM32_IDLE, I2C_STM32_TX, I2C_STM32_RX, I2C_STM32_DONE,
    I2C_STM32_FAILED, I2C_STM32_CANCELLING, I2C_STM32_ABORTED,
    I2C_STM32_WAITER_LEASE_MS = 30U,
    I2C_STM32_BUSY_RECOVERY_MS = 40U,
    I2C_STM32_CANCEL_TIMEOUT_MS = 30U
};

static uint32_t i2c_stm32_lock(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void i2c_stm32_unlock(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/* Call with interrupts masked. Reset only I2C1 and drop pending I2C IRQs.
 * Registers are reconfigured using the existing clocks, pins and HAL settings.
 * No GPIO bus clearing is attempted when a peripheral holds a line low. */
static void i2c_stm32_recover(i2c_stm32_t *backend)
{
    I2C_HandleTypeDef *const hal = backend->hal;
    __HAL_I2C_DISABLE_IT(hal, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR);
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();
    hal->pBuffPtr = NULL;
    hal->XferCount = 0U;
    hal->XferSize = 0U;
    hal->State = HAL_I2C_STATE_READY;
    backend->ready = HAL_I2C_Init(hal) == HAL_OK;
    HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
    HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);
    backend->busy_observed = false;
}

static bool i2c_stm32_bus_idle(i2c_stm32_t *backend, uint32_t now)
{
    I2C_HandleTypeDef *const hal = backend->hal;
    if (HAL_I2C_GetState(hal) == HAL_I2C_STATE_READY
        && (hal->Instance->CR1 & I2C_CR1_STOP) == 0U
        && __HAL_I2C_GET_FLAG(hal, I2C_FLAG_BUSY) == RESET) {
        backend->busy_observed = false;
        return true;
    }
    if (!backend->busy_observed) {
        backend->busy_observed = true;
        backend->busy_ms = now;
    } else if (now - backend->busy_ms >= I2C_STM32_BUSY_RECOVERY_MS) {
        i2c_stm32_recover(backend);
    }
    return false;
}

static i2c_async_result_t i2c_stm32_submit(i2c_stm32_t *backend, const void *owner,
    uint8_t address, const uint8_t *source, uint8_t *destination, size_t length,
    i2c_async_token_t *token)
{
    if (backend == NULL || !backend->ready || owner == NULL || token == NULL
        || address < 0x08U || address > 0x77U || length == 0U
        || length > I2C_STM32_MAX_TRANSFER_BYTES || (source == NULL && destination == NULL)
        || backend->token == UINT32_MAX) {
        return I2C_ASYNC_ERROR;
    }

    const uint32_t now = HAL_GetTick();
    if (backend->owner != NULL) {
        if (owner != backend->owner) {
            backend->waiter = owner;
            backend->waiter_ms = now;
        }
        return I2C_ASYNC_BUSY;
    }
    if (backend->waiter != NULL && owner != backend->waiter
        && now - backend->waiter_ms < I2C_STM32_WAITER_LEASE_MS) {
        return I2C_ASYNC_BUSY;
    }
    if (source != NULL) {
        memcpy(backend->staging, source, length);
    }

    const uint32_t primask = i2c_stm32_lock();
    if (!i2c_stm32_bus_idle(backend, now)) {
        i2c_stm32_unlock(primask);
        return I2C_ASYNC_BUSY;
    }

    backend->state = source != NULL ? I2C_STM32_TX : I2C_STM32_RX;
    const uint16_t shifted_address = (uint16_t)((uint16_t)address << 1U);
    /* F1 HAL's plain Master_*_IT busy-waits on BUSY. The sequential OTHER
     * option enters with a START and skips that loop when STOP is clear.
     * EV IRQ converts it to FIRST_AND_LAST, which emits the final STOP. */
    const HAL_StatusTypeDef status = source != NULL
        ? HAL_I2C_Master_Seq_Transmit_IT(backend->hal, shifted_address,
            backend->staging, (uint16_t)length, I2C_OTHER_AND_LAST_FRAME)
        : HAL_I2C_Master_Seq_Receive_IT(backend->hal, shifted_address,
            backend->staging, (uint16_t)length, I2C_OTHER_AND_LAST_FRAME);
    if (status == HAL_OK) {
        backend->owner = owner;
        backend->waiter = NULL;
        backend->length = (uint16_t)length;
        backend->receive_destination = destination;
        *token = ++backend->token;
    } else {
        backend->state = I2C_STM32_IDLE;
    }
    i2c_stm32_unlock(primask);
    if (status == HAL_OK) {
        return I2C_ASYNC_ACCEPTED;
    }
    return status == HAL_BUSY ? I2C_ASYNC_BUSY : I2C_ASYNC_ERROR;
}

static i2c_async_result_t i2c_stm32_transmit(void *context, const void *owner,
    uint8_t address, const uint8_t *data, size_t length, i2c_async_token_t *token)
{
    return i2c_stm32_submit(context, owner, address, data, NULL, length, token);
}

static i2c_async_result_t i2c_stm32_receive(void *context, const void *owner,
    uint8_t address, uint8_t *data, size_t length, i2c_async_token_t *token)
{
    return i2c_stm32_submit(context, owner, address, NULL, data, length, token);
}

/* Call with I2C IRQs masked. The HAL buffer is private, so copy successful
 * RX data out only after completion and never expose partial reads. */
static i2c_async_result_t i2c_stm32_retire(i2c_stm32_t *backend, bool completed)
{
    if (!completed) {
        i2c_stm32_recover(backend);
    } else if (backend->receive_destination != NULL) {
        memcpy(backend->receive_destination, backend->staging, backend->length);
    }
    backend->owner = NULL;
    backend->receive_destination = NULL;
    backend->state = I2C_STM32_IDLE;
    return completed ? I2C_ASYNC_COMPLETE : I2C_ASYNC_ERROR;
}

static i2c_async_result_t i2c_stm32_finish(void *context, const void *owner,
    i2c_async_token_t token, bool cancel)
{
    i2c_stm32_t *const backend = context;
    if (backend == NULL || owner == NULL || owner != backend->owner
        || token == 0U || token != backend->token) {
        return I2C_ASYNC_ERROR;
    }

    const uint32_t now = HAL_GetTick();
    const uint32_t primask = i2c_stm32_lock();
    i2c_async_result_t result = I2C_ASYNC_PENDING;
    switch (backend->state) {
    case I2C_STM32_DONE:
        result = i2c_stm32_retire(backend, true);
        break;
    case I2C_STM32_FAILED:
    case I2C_STM32_ABORTED:
        result = i2c_stm32_retire(backend, false);
        break;
    case I2C_STM32_CANCELLING:
        if (now - backend->cancel_ms >= I2C_STM32_CANCEL_TIMEOUT_MS) {
            result = i2c_stm32_retire(backend, false);
        }
        break;
    default:
        if (cancel) {
            backend->cancel_ms = now;
            backend->state = I2C_STM32_CANCELLING;
            const uint16_t address = (uint16_t)backend->hal->Devaddress;
            if (HAL_I2C_Master_Abort_IT(backend->hal, address) != HAL_OK) {
                result = i2c_stm32_retire(backend, false);
            }
        }
        break;
    }
    i2c_stm32_unlock(primask);
    return result;
}

static i2c_async_result_t i2c_stm32_poll(void *context, const void *owner,
    i2c_async_token_t token)
{
    return i2c_stm32_finish(context, owner, token, false);
}

static i2c_async_result_t i2c_stm32_cancel(void *context, const void *owner,
    i2c_async_token_t token)
{
    return i2c_stm32_finish(context, owner, token, true);
}

app_status_t i2c_stm32_init(i2c_stm32_t *backend, I2C_HandleTypeDef *hal,
                            i2c_async_t *bus)
{
    if (backend == NULL || hal == NULL || bus == NULL || hal->Instance != I2C1
        || HAL_I2C_GetState(hal) != HAL_I2C_STATE_READY) {
        return APP_INVALID_ARGUMENT;
    }
    if (backend->hal != NULL) {
        return APP_BUSY;
    }
    *backend = (i2c_stm32_t){.hal = hal, .ready = true};
    *bus = (i2c_async_t){backend, i2c_stm32_transmit, i2c_stm32_receive,
                        i2c_stm32_poll, i2c_stm32_cancel};
    return APP_OK;
}

void i2c_stm32_notify(i2c_stm32_t *backend, const I2C_HandleTypeDef *hal,
                       i2c_stm32_event_t event)
{
    if (backend == NULL || hal == NULL || hal != backend->hal) {
        return;
    }
    if (backend->state == I2C_STM32_CANCELLING) {
        backend->state = I2C_STM32_ABORTED;
    } else if (backend->state == I2C_STM32_TX || backend->state == I2C_STM32_RX) {
        if ((backend->state == I2C_STM32_TX && event == I2C_STM32_TX_COMPLETE)
            || (backend->state == I2C_STM32_RX && event == I2C_STM32_RX_COMPLETE)) {
            backend->state = hal->XferCount == 0U ? I2C_STM32_DONE : I2C_STM32_FAILED;
        } else if (event == I2C_STM32_ERROR || event == I2C_STM32_ABORT_COMPLETE) {
            backend->state = I2C_STM32_FAILED;
        }
    }
}
