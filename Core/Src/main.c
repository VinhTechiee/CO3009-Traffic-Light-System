/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "button.h"
#include "fsm_mode.h"
#include "scheduler.h"
#include "software_timer.h"
#include "traffic_light.h"
#include <stddef.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Set a bit for each button measured low when pressed. The default runs Auto with button input disabled. */
#define CO3009_BUTTON_MASK_UNVERIFIED 0xFFU
#ifndef CO3009_BUTTON_ACTIVE_LOW_MASK
#define CO3009_BUTTON_ACTIVE_LOW_MASK CO3009_BUTTON_MASK_UNVERIFIED
#endif
_Static_assert(CO3009_BUTTON_ACTIVE_LOW_MASK == CO3009_BUTTON_MASK_UNVERIFIED
    || (unsigned)CO3009_BUTTON_ACTIVE_LOW_MASK <= BUTTON_MASK_ALL, "Invalid button polarity mask");
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
enum {
  CO3009_TICK_MS = 10U,
  CO3009_DEMO_GREEN_MS = 3000U,
  CO3009_DEMO_YELLOW_MS = 2000U,
  CO3009_MIN_COLOR_MS = 1000U,
  CO3009_MAX_GREEN_MS = 30000U,
  CO3009_MAX_YELLOW_MS = 10000U,
  CO3009_TUNING_STEP_MS = 1000U
};

/* Tuning currently requires RED = GREEN + YELLOW. Use that common subset
 * even though Automatic also supports a positive all-red clearance. */
static const timing_config_t co3009_initial_timing = {
  .red_ms = CO3009_DEMO_GREEN_MS + CO3009_DEMO_YELLOW_MS,
  .yellow_ms = CO3009_DEMO_YELLOW_MS,
  .green_ms = CO3009_DEMO_GREEN_MS
};

static bool co3009_decode_light(uint32_t outputs, uint16_t pin_a, uint16_t pin_b,
                                traffic_color_t *color)
{
  const bool a_high = (outputs & pin_a) != 0U;
  const bool b_high = (outputs & pin_b) != 0U;

  if (a_high) {
    *color = b_high ? TRAFFIC_GREEN : TRAFFIC_YELLOW;
  } else if (b_high) {
    *color = TRAFFIC_RED;
  } else {
    return false;
  }
  return true;
}

static app_status_t co3009_get_automatic_phase(traffic_state_t *phase)
{
  traffic_state_t snapshot;

  if (phase == NULL) {
    return APP_INVALID_ARGUMENT;
  }

  /* Both roads share GPIOB. Read one snapshot of the output latch before
   * fsm_mode exits Auto and changes the lights to all-red. */
  const uint32_t outputs = GPIOB->ODR;
  if (!co3009_decode_light(outputs, TL1_A_Pin, TL1_B_Pin, &snapshot.road_1)
      || !co3009_decode_light(outputs, TL2_A_Pin, TL2_B_Pin, &snapshot.road_2)
      || (snapshot.road_1 == TRAFFIC_GREEN && snapshot.road_2 == TRAFFIC_GREEN)) {
    return APP_IO_ERROR;
  }
  *phase = snapshot;
  return APP_OK;
}

static bool co3009_tim2_tick_matches(void)
{
  RCC_ClkInitTypeDef clocks = {0};
  uint32_t flash_latency;

  HAL_RCC_GetClockConfig(&clocks, &flash_latency);
  uint64_t timer_hz = HAL_RCC_GetPCLK1Freq();
  if (clocks.APB1CLKDivider != RCC_HCLK_DIV1) {
    timer_hz *= 2U;
  }

  const uint64_t timer_counts =
      ((uint64_t)htim2.Init.Prescaler + 1U) * ((uint64_t)htim2.Init.Period + 1U);
  return timer_hz != 0U && timer_counts * 1000U == timer_hz * CO3009_TICK_MS;
}

static const fsm_mode_config_t co3009_mode_config = {
  .timing = {
    .red = {2U * CO3009_MIN_COLOR_MS, CO3009_MAX_GREEN_MS + CO3009_MAX_YELLOW_MS},
    .green = {CO3009_MIN_COLOR_MS, CO3009_MAX_GREEN_MS},
    .yellow = {CO3009_MIN_COLOR_MS, CO3009_MAX_YELLOW_MS},
    .step_ms = CO3009_TUNING_STEP_MS,
    .red_adjusts = TRAFFIC_GREEN
  },
  .after_confirm = APP_MODE_AUTOMATIC,
  .after_cancel = APP_MODE_AUTOMATIC,
  .automatic_phase = co3009_get_automatic_phase,
  .bindings = {
    [APP_MODE_AUTOMATIC] = {
      {.kind = FSM_MODE_SWITCH, .button = BUTTON_ID_1,
       .trigger = BUTTON_EVENT_PRESS, .mode = APP_MODE_MANUAL}
    },
    [APP_MODE_MANUAL] = {
      {.kind = FSM_MODE_SWITCH, .button = BUTTON_ID_1,
       .trigger = BUTTON_EVENT_PRESS, .mode = APP_MODE_TUNING}
    },
    [APP_MODE_TUNING] = {
      {.kind = FSM_MODE_SWITCH, .button = BUTTON_ID_1,
       .trigger = BUTTON_EVENT_PRESS, .mode = APP_MODE_AUTOMATIC},
      {.kind = FSM_MODE_TUNING_ACTION, .button = BUTTON_ID_2,
       .trigger = BUTTON_EVENT_SHORT, .action = FSM_TUNING_NEXT},
      {.kind = FSM_MODE_TUNING_ACTION, .button = BUTTON_ID_2,
       .trigger = BUTTON_EVENT_LONG, .action = FSM_TUNING_CONFIRM},
      {.kind = FSM_MODE_TUNING_ACTION, .button = BUTTON_ID_3,
       .trigger = BUTTON_EVENT_PRESS, .action = FSM_TUNING_INCREASE},
      {.kind = FSM_MODE_TUNING_ACTION, .button = BUTTON_ID_4,
       .trigger = BUTTON_EVENT_PRESS, .action = FSM_TUNING_DECREASE}
    }
  }
};

static app_status_t co3009_start_application(void)
{
  const bool buttons_enabled =
      CO3009_BUTTON_ACTIVE_LOW_MASK != CO3009_BUTTON_MASK_UNVERIFIED;
  const sch_task_fn_t tasks[] = {button_task, software_timer_tick, fsm_mode_task};
  sch_task_id_t task_ids[sizeof tasks / sizeof tasks[0]];
  size_t task_count = 0U;

  app_status_t status = traffic_light_init();
  if (status != APP_OK) {
    return status;
  }

  if (!co3009_tim2_tick_matches()) {
    return APP_INVALID_ARGUMENT;
  }

  if (buttons_enabled) {
    status = button_configure_active_low((uint8_t)CO3009_BUTTON_ACTIVE_LOW_MASK);
    if (status != APP_OK) {
      return status;
    }
    status = button_init();
    if (status != APP_OK) {
      return status;
    }
  }

  SCH_Init();
  software_timer_init();
  status = fsm_mode_configure(&co3009_mode_config);
  if (status != APP_OK) {
    return status;
  }

  /* All these foreground callbacks run once per 10 ms TIM2 tick. */
  for (size_t i = buttons_enabled ? 0U : 1U; i < sizeof tasks / sizeof tasks[0]; ++i) {
    status = SCH_Add_Task(tasks[i], 1U, 1U, &task_ids[task_count]);
    if (status != APP_OK) {
      break;
    }
    ++task_count;
  }
  if (status == APP_OK) {
    status = fsm_mode_init(APP_MODE_AUTOMATIC, &co3009_initial_timing);
  }
  if (status == APP_OK) {
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
    if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK) {
      status = APP_IO_ERROR;
    }
  }
  if (status != APP_OK) {
    while (task_count > 0U) {
      --task_count;
      (void)SCH_Delete_Task(task_ids[task_count]);
    }
  }
  return status;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  if (co3009_start_application() != APP_OK) {
    const traffic_state_t safe = {TRAFFIC_RED, TRAFFIC_RED};
    (void)traffic_light_apply(&safe);
    Error_Handler();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    SCH_Dispatch_Tasks();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 7999;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 79;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_Debug_A_GPIO_Port, LED_Debug_A_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, TL2_B_Pin|TL1_A_Pin|TL2_A_Pin|TL1_B_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_Debug_B_GPIO_Port, LED_Debug_B_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : BTN_USER_Pin BTN_3_Pin */
  GPIO_InitStruct.Pin = BTN_USER_Pin|BTN_3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : LED_Debug_A_Pin */
  GPIO_InitStruct.Pin = LED_Debug_A_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_Debug_A_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : TL2_B_Pin TL1_A_Pin TL2_A_Pin TL1_B_Pin */
  GPIO_InitStruct.Pin = TL2_B_Pin|TL1_A_Pin|TL2_A_Pin|TL1_B_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : LED_Debug_B_Pin */
  GPIO_InitStruct.Pin = LED_Debug_B_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_Debug_B_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BTN_1_Pin BTN_2_Pin */
  GPIO_InitStruct.Pin = BTN_1_Pin|BTN_2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : BTN_4_Pin */
  GPIO_InitStruct.Pin = BTN_4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BTN_4_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2) {
    SCH_Update();
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
