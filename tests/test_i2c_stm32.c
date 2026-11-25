#include "i2c_stm32.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

I2C_TypeDef fake_i2c1;
static I2C_HandleTypeDef fake_hal = {.Instance = I2C1, .State = HAL_I2C_STATE_READY};
static i2c_stm32_t backend;
static i2c_async_t bus;
static uint32_t now_ms, primask;
static unsigned starts, resets, pending_clears, aborts;
static bool line_busy, reset_asserted, is_rx;
static HAL_StatusTypeDef next_start = HAL_OK;
static enum { ABORT_SYNC, ABORT_LOST, ABORT_REJECTED } abort_behavior;

uint32_t __get_PRIMASK(void) { return primask; }
void __disable_irq(void) { primask = 1U; }
void __set_PRIMASK(uint32_t mask) { primask = mask; }
uint32_t HAL_GetTick(void) { return now_ms; }

void fake_i2c_force_reset(void)
{
    assert(primask == 1U && !reset_asserted);
    reset_asserted = true;
    ++resets;
}

void fake_i2c_release_reset(void)
{
    assert(primask == 1U && reset_asserted);
    reset_asserted = false;
    fake_i2c1.CR1 = 0U;
}

void fake_i2c_disable_it(I2C_HandleTypeDef *hal, unsigned flags)
{
    assert(hal == &fake_hal && primask == 1U && flags == 7U);
}

unsigned fake_i2c_get_flag(I2C_HandleTypeDef *hal, unsigned flag)
{
    assert(hal == &fake_hal && flag == I2C_FLAG_BUSY);
    return line_busy;
}

void HAL_NVIC_ClearPendingIRQ(int irq)
{
    assert(primask == 1U && (irq == I2C1_EV_IRQn || irq == I2C1_ER_IRQn));
    ++pending_clears;
}

HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *hal)
{
    return hal->State;
}

HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *hal)
{
    assert(primask == 1U && hal == &fake_hal && hal->pBuffPtr == NULL);
    assert(hal->State == HAL_I2C_STATE_READY && hal->XferCount == 0U);
    return HAL_OK;
}

static HAL_StatusTypeDef start(I2C_HandleTypeDef *hal, uint16_t address,
    uint8_t *buffer, uint16_t length, uint32_t options, bool receive)
{
    assert(primask == 1U && hal == &fake_hal && !line_busy);
    assert(options == I2C_OTHER_AND_LAST_FRAME && (fake_i2c1.CR1 & I2C_CR1_STOP) == 0U);
    ++starts;
    if (next_start != HAL_OK) {
        const HAL_StatusTypeDef status = next_start;
        next_start = HAL_OK;
        return status;
    }
    is_rx = receive;
    hal->Devaddress = address;
    hal->pBuffPtr = buffer;
    hal->State = HAL_I2C_STATE_BUSY;
    hal->XferCount = length;
    hal->XferSize = length;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Master_Seq_Transmit_IT(I2C_HandleTypeDef *hal, uint16_t address,
    uint8_t *buffer, uint16_t length, uint32_t options)
{
    return start(hal, address, buffer, length, options, false);
}

HAL_StatusTypeDef HAL_I2C_Master_Seq_Receive_IT(I2C_HandleTypeDef *hal, uint16_t address,
    uint8_t *buffer, uint16_t length, uint32_t options)
{
    return start(hal, address, buffer, length, options, true);
}

HAL_StatusTypeDef HAL_I2C_Master_Abort_IT(I2C_HandleTypeDef *hal, uint16_t address)
{
    assert(primask == 1U && hal == &fake_hal && address == hal->Devaddress);
    ++aborts;
    if (abort_behavior == ABORT_SYNC) {
        /* F1 HAL can write one residual byte beyond the transferred bytes. */
        hal->pBuffPtr[hal->XferSize] = 0x55U;
        i2c_stm32_notify(&backend, hal, I2C_STM32_ABORT_COMPLETE);
    }
    return abort_behavior == ABORT_REJECTED ? HAL_ERROR : HAL_OK;
}

static void complete(void)
{
    fake_hal.State = HAL_I2C_STATE_READY;
    fake_hal.XferCount = 0U;
    i2c_stm32_notify(&backend, &fake_hal, is_rx ? I2C_STM32_RX_COMPLETE : I2C_STM32_TX_COMPLETE);
}

int main(void)
{
    static const char lcd = 'L', sensor = 'S';
    uint8_t tx[] = {0xACU, 0x33U, 0x00U};
    uint8_t rx[4] = {0xFFU, 0xFFU, 0xFFU, 0x11U};
    i2c_async_token_t sensor_token = 0U, lcd_token = 0U;
    assert(i2c_stm32_init(NULL, &fake_hal, &bus) == APP_INVALID_ARGUMENT);
    assert(i2c_stm32_init(&backend, &fake_hal, &bus) == APP_OK);
    assert(i2c_stm32_init(&backend, &fake_hal, &bus) == APP_BUSY);
    assert(bus.transmit(bus.context, NULL, 0x38U, tx, 3U, &sensor_token) == I2C_ASYNC_ERROR);
    assert(bus.transmit(bus.context, &sensor, 0x78U, tx, 3U, &sensor_token) == I2C_ASYNC_ERROR);
    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 49U, &sensor_token) == I2C_ASYNC_ERROR);
    assert(starts == 0U && sensor_token == 0U);

    next_start = HAL_BUSY;
    assert(bus.transmit(bus.context, &sensor, 0x38U, tx, 3U, &sensor_token) == I2C_ASYNC_BUSY);
    assert(sensor_token == 0U);
    assert(bus.transmit(bus.context, &sensor, 0x38U, tx, 3U, &sensor_token) == I2C_ASYNC_ACCEPTED);
    assert(sensor_token != 0U && primask == 0U && fake_hal.Devaddress == 0x70U);
    assert(fake_hal.pBuffPtr != tx && memcmp(fake_hal.pBuffPtr, tx, 3U) == 0);
    assert(bus.transmit(bus.context, &lcd, 0x27U, tx, 3U, &lcd_token) == I2C_ASYNC_BUSY);
    assert(lcd_token == 0U && starts == 2U);
    assert(bus.cancel(bus.context, &lcd, sensor_token) == I2C_ASYNC_ERROR && aborts == 0U);
    I2C_HandleTypeDef other = fake_hal;
    i2c_stm32_notify(&backend, &other, I2C_STM32_TX_COMPLETE);
    i2c_stm32_notify(&backend, &fake_hal, I2C_STM32_RX_COMPLETE);
    assert(bus.poll(bus.context, &sensor, sensor_token) == I2C_ASYNC_PENDING);
    complete();
    assert(bus.transmit(bus.context, &lcd, 0x27U, tx, 3U, &lcd_token) == I2C_ASYNC_BUSY);
    assert(bus.poll(bus.context, &sensor, sensor_token) == I2C_ASYNC_COMPLETE);
    assert(bus.poll(bus.context, &sensor, sensor_token) == I2C_ASYNC_ERROR);
    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 3U, &sensor_token) == I2C_ASYNC_BUSY);
    assert(bus.transmit(bus.context, &lcd, 0x27U, tx, 3U, &lcd_token) == I2C_ASYNC_ACCEPTED);
    assert(fake_hal.Devaddress == 0x4EU && lcd_token != sensor_token);
    complete();
    assert(bus.poll(bus.context, &lcd, lcd_token) == I2C_ASYNC_COMPLETE);

    const i2c_async_token_t stale = sensor_token;
    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 3U, &sensor_token) == I2C_ASYNC_ACCEPTED);
    assert(is_rx && fake_hal.pBuffPtr != rx && bus.cancel(bus.context, &sensor, stale) == I2C_ASYNC_ERROR);
    memcpy(fake_hal.pBuffPtr, "abc", 3U);
    assert(rx[0] == 0xFFU);
    complete();
    assert(bus.poll(bus.context, &sensor, sensor_token) == I2C_ASYNC_COMPLETE);
    assert(memcmp(rx, "abc", 3U) == 0 && rx[3] == 0x11U);

    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 3U, &sensor_token) == I2C_ASYNC_ACCEPTED);
    fake_hal.pBuffPtr[0] = 0x01U;
    i2c_stm32_notify(&backend, &fake_hal, I2C_STM32_RX_COMPLETE); /* Short transfer. */
    assert(bus.poll(bus.context, &sensor, sensor_token) == I2C_ASYNC_ERROR);
    assert(resets == 1U && memcmp(rx, "abc", 3U) == 0);
    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 3U, &sensor_token) == I2C_ASYNC_ACCEPTED);
    i2c_stm32_notify(&backend, &fake_hal, I2C_STM32_ERROR);
    assert(bus.poll(bus.context, &sensor, sensor_token) == I2C_ASYNC_ERROR && resets == 2U);

    uint8_t maximum[I2C_STM32_MAX_TRANSFER_BYTES] = {0U};
    assert(bus.receive(bus.context, &sensor, 0x38U, maximum, sizeof maximum, &sensor_token)
           == I2C_ASYNC_ACCEPTED);
    assert(bus.cancel(bus.context, &sensor, sensor_token) == I2C_ASYNC_PENDING && aborts == 1U);
    assert(bus.poll(bus.context, &sensor, sensor_token) == I2C_ASYNC_ERROR && resets == 3U);
    assert(maximum[sizeof maximum - 1U] == 0U);

    abort_behavior = ABORT_LOST;
    now_ms = UINT32_MAX - 20U;
    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 1U, &sensor_token) == I2C_ASYNC_ACCEPTED);
    assert(bus.cancel(bus.context, &sensor, sensor_token) == I2C_ASYNC_PENDING);
    now_ms += 29U;
    assert(bus.poll(bus.context, &sensor, sensor_token) == I2C_ASYNC_PENDING && resets == 3U);
    ++now_ms;
    assert(bus.poll(bus.context, &sensor, sensor_token) == I2C_ASYNC_ERROR && resets == 4U);
    i2c_stm32_notify(&backend, &fake_hal, I2C_STM32_ABORT_COMPLETE);
    abort_behavior = ABORT_REJECTED;
    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 1U, &sensor_token) == I2C_ASYNC_ACCEPTED);
    assert(bus.cancel(bus.context, &sensor, sensor_token) == I2C_ASYNC_ERROR && resets == 5U);

    line_busy = true;
    const unsigned previous_starts = starts;
    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 1U, &sensor_token) == I2C_ASYNC_BUSY);
    now_ms += 39U;
    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 1U, &sensor_token) == I2C_ASYNC_BUSY);
    ++now_ms;
    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 1U, &sensor_token) == I2C_ASYNC_BUSY);
    assert(resets == 6U && starts == previous_starts && pending_clears == 2U * resets);
    line_busy = false;
    primask = 1U;
    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 1U, &sensor_token) == I2C_ASYNC_ACCEPTED);
    assert(primask == 1U);
    primask = 0U;
    assert(bus.transmit(bus.context, &lcd, 0x27U, tx, 3U, &lcd_token) == I2C_ASYNC_BUSY);
    complete();
    assert(bus.poll(bus.context, &sensor, sensor_token) == I2C_ASYNC_COMPLETE);
    now_ms += 30U;
    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 1U, &sensor_token) == I2C_ASYNC_ACCEPTED);
    complete();
    assert(bus.cancel(bus.context, &sensor, sensor_token) == I2C_ASYNC_COMPLETE);
    backend.token = UINT32_MAX;
    assert(bus.receive(bus.context, &sensor, 0x38U, rx, 1U, &sensor_token) == I2C_ASYNC_ERROR);
    puts("STM32 I2C tests passed: ownership, arbitration, buffers, errors, cancel and recovery");
    return 0;
}
