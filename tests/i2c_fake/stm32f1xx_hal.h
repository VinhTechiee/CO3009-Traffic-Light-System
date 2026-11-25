#ifndef TEST_I2C_STM32F1XX_HAL_H
#define TEST_I2C_STM32F1XX_HAL_H

#include <stdint.h>

typedef struct { uint32_t CR1; } I2C_TypeDef;
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY } HAL_StatusTypeDef;
typedef enum { HAL_I2C_STATE_READY, HAL_I2C_STATE_BUSY } HAL_I2C_StateTypeDef;
typedef struct {
    I2C_TypeDef *Instance;
    HAL_I2C_StateTypeDef State;
    uint8_t *pBuffPtr;
    uint16_t XferCount, XferSize;
    uint32_t Devaddress;
} I2C_HandleTypeDef;

enum { RESET = 0U, I2C_IT_EVT = 1U, I2C_IT_BUF = 2U, I2C_IT_ERR = 4U,
    I2C_CR1_STOP = 1U, I2C_FLAG_BUSY = 1U, I2C_OTHER_AND_LAST_FRAME = 1U,
    I2C1_EV_IRQn = 31, I2C1_ER_IRQn = 32 };

extern I2C_TypeDef fake_i2c1;
#define I2C1 (&fake_i2c1)
#define __HAL_RCC_I2C1_FORCE_RESET() fake_i2c_force_reset()
#define __HAL_RCC_I2C1_RELEASE_RESET() fake_i2c_release_reset()
#define __HAL_I2C_DISABLE_IT(handle, flags) fake_i2c_disable_it(handle, flags)
#define __HAL_I2C_GET_FLAG(handle, flag) fake_i2c_get_flag(handle, flag)

uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
uint32_t HAL_GetTick(void);
void fake_i2c_force_reset(void);
void fake_i2c_release_reset(void);
void fake_i2c_disable_it(I2C_HandleTypeDef *hal, unsigned flags);
unsigned fake_i2c_get_flag(I2C_HandleTypeDef *hal, unsigned flag);
void HAL_NVIC_ClearPendingIRQ(int irq);
HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *hal);
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *hal);
HAL_StatusTypeDef HAL_I2C_Master_Seq_Transmit_IT(I2C_HandleTypeDef *hal, uint16_t address,
    uint8_t *buffer, uint16_t length, uint32_t options);
HAL_StatusTypeDef HAL_I2C_Master_Seq_Receive_IT(I2C_HandleTypeDef *hal, uint16_t address,
    uint8_t *buffer, uint16_t length, uint32_t options);
HAL_StatusTypeDef HAL_I2C_Master_Abort_IT(I2C_HandleTypeDef *hal, uint16_t address);

#endif /* TEST_I2C_STM32F1XX_HAL_H */
