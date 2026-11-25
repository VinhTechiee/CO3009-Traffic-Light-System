#include "board_diagnostic.h"

#include <stddef.h>

#include "button.h"
#include "main.h"
#include "scheduler.h"

/* Results are read over SWD from the diagnostic ELF. */
volatile uint8_t board_diagnostic_i2c_ack[128];
volatile bool board_diagnostic_i2c_scan_finished;
volatile uint8_t board_diagnostic_button_high_mask;
volatile uint32_t board_diagnostic_tick_callbacks;
volatile uint32_t board_diagnostic_one_shots;
volatile uint64_t board_diagnostic_one_shot_tick;

static uint8_t button_level(GPIO_TypeDef *port, uint16_t pin, button_id_t id)
{
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET ? BUTTON_MASK(id) : 0U;
}

static void board_diagnostic_tick(void)
{
    ++board_diagnostic_tick_callbacks;
    HAL_GPIO_TogglePin(LED_Debug_A_GPIO_Port, LED_Debug_A_Pin);
    board_diagnostic_button_high_mask =
        (uint8_t)(button_level(BTN_1_GPIO_Port, BTN_1_Pin, BUTTON_ID_1)
            | button_level(BTN_2_GPIO_Port, BTN_2_Pin, BUTTON_ID_2)
            | button_level(BTN_3_GPIO_Port, BTN_3_Pin, BUTTON_ID_3)
            | button_level(BTN_4_GPIO_Port, BTN_4_Pin, BUTTON_ID_4)
            | button_level(BTN_USER_GPIO_Port, BTN_USER_Pin, BUTTON_ID_USER));
}

static void board_diagnostic_one_shot(void)
{
    board_diagnostic_one_shot_tick = SCH_Now();
    ++board_diagnostic_one_shots;
    HAL_GPIO_WritePin(LED_Debug_B_GPIO_Port, LED_Debug_B_Pin, GPIO_PIN_SET);
}

app_status_t board_diagnostic_start(I2C_HandleTypeDef *i2c)
{
    if (i2c != NULL) {
        /* Diagnostic boot scan only, before starting TIM2 or dispatching tasks. */
        for (uint16_t address = 0x08U; address <= 0x77U; ++address) {
            board_diagnostic_i2c_ack[address] =
                HAL_I2C_IsDeviceReady(i2c, (uint16_t)(address << 1U), 1U, 3U) == HAL_OK;
        }
        board_diagnostic_i2c_scan_finished = true;
    }

    SCH_Init();
    sch_task_id_t periodic_id;
    app_status_t status = SCH_Add_Task(board_diagnostic_tick, 1U, 1U, &periodic_id);
    if (status == APP_OK) {
        sch_task_id_t one_shot_id;
        status = SCH_Add_Task(board_diagnostic_one_shot, 25U, 0U, &one_shot_id);
        if (status != APP_OK) {
            (void)SCH_Delete_Task(periodic_id);
        }
    }
    return status;
}
