#include "main.h"

void HAL_MspInit(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    /* PB3 and PB4 belong to the shield; retain PA13/PA14 for SWD. */
    __HAL_AFIO_REMAP_SWJ_NOJTAG();
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *handle)
{
    if (handle->Instance != I2C1) {
        return;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* SWJ_CFG readback is undefined. Explicitly retain SWD when remapping I2C1. */
    MODIFY_REG(AFIO->MAPR, AFIO_MAPR_SWJ_CFG | AFIO_MAPR_I2C1_REMAP,
               AFIO_MAPR_SWJ_CFG_JTAGDISABLE | AFIO_MAPR_I2C1_REMAP);
    GPIO_InitTypeDef bus = {
        .Pin = GPIO_PIN_8 | GPIO_PIN_9,
        .Mode = GPIO_MODE_AF_OD,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_HIGH
    };
    HAL_GPIO_Init(GPIOB, &bus);
    __HAL_RCC_I2C1_CLK_ENABLE();

    /* Equal priorities keep the HAL event and error handlers serialized. */
    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 1U, 0U);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef *handle)
{
    if (handle->Instance != I2C1) {
        return;
    }
    HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
    HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
    HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);
    __HAL_RCC_I2C1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_8 | GPIO_PIN_9);
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *handle)
{
    if (handle->Instance == TIM2) {
        __HAL_RCC_TIM2_CLK_ENABLE();
        HAL_NVIC_SetPriority(TIM2_IRQn, 0U, 0U);
        HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);
        HAL_NVIC_EnableIRQ(TIM2_IRQn);
    }
}

void HAL_UART_MspInit(UART_HandleTypeDef *handle)
{
    if (handle->Instance != USART2) {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    GPIO_InitTypeDef uart = {
        .Pin = USART_TX_Pin,
        .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW
    };
    HAL_GPIO_Init(USART_TX_GPIO_Port, &uart);
    uart.Pin = USART_RX_Pin;
    uart.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(USART_RX_GPIO_Port, &uart);
}
