/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <stdint.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Піни (мають збігатися з servoblink.ioc) */
#define BTN_PORT                GPIOA
#define BTN_PIN                 GPIO_PIN_0          /* кнопка на GND, внутрішня підтяжка */
#define BTN_PRESSED_LEVEL       GPIO_PIN_RESET      /* натиснута = низький рівень */

#define LED_EXT_PORT            GPIOA
#define LED_EXT_PIN             GPIO_PIN_1          /* зовнішній LED через 220 Ом */

#define LED_BUILTIN_PORT        GPIOC
#define LED_BUILTIN_PIN         GPIO_PIN_13         /* вбудований LED Black Pill */
#define LED_BUILTIN_ON_LEVEL    GPIO_PIN_RESET      /* на PC13 LED вмикається низьким рівнем */
#define LED_BUILTIN_OFF_LEVEL   GPIO_PIN_SET

#define SERVO_TIM_CHANNEL       TIM_CHANNEL_1       /* TIM3_CH1 = PA6 */

/* Часові параметри, мс */
#define BTN_DEBOUNCE_MS         50U     /* стан кнопки має бути стабільним стільки часу */
#define LED_BLINK_TOGGLE_MS     300U    /* півперіод мигання зовнішнього LED */
#define SERVO_STEP_PERIOD_MS    20U     /* як часто зсувається серва (= період PWM 50 Гц) */

/* Імпульс серви, мкс. TIM3 тактується 1 МГц (HSI 16 МГц / (15 + 1)),
   тому значення compare дорівнює ширині імпульсу в мікросекундах. */
#define SERVO_PULSE_MIN_US      1000U
#define SERVO_PULSE_MAX_US      2000U
#define SERVO_PULSE_CENTER_US   1500U
#define SERVO_PULSE_STEP_US     20U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */
static bool Button_WasPressed(uint32_t now);
static void System_SetOn(bool on);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Стан системи: false — вимкнено (серва в центрі, LED не мигає) */
static bool systemOn = false;

/* Детект фронту з антидребезгом: true лише один раз на кожне натискання.
   Новий рівень приймається, якщо він тримався не менше BTN_DEBOUNCE_MS. */
static bool Button_WasPressed(uint32_t now)
{
  static GPIO_PinState rawLast = GPIO_PIN_SET;     /* відпущена (pull-up) */
  static GPIO_PinState stable  = GPIO_PIN_SET;
  static uint32_t lastChangeMs = 0U;

  const GPIO_PinState raw = HAL_GPIO_ReadPin(BTN_PORT, BTN_PIN);

  if (raw != rawLast)
  {
    rawLast = raw;
    lastChangeMs = now;
  }

  if ((now - lastChangeMs) >= BTN_DEBOUNCE_MS && raw != stable)
  {
    stable = raw;
    return (stable == BTN_PRESSED_LEVEL);
  }

  return false;
}

/* Увімкнення/вимкнення системи. При вимкненні LED гасне, серва в центр. */
static void System_SetOn(bool on)
{
  systemOn = on;

  HAL_GPIO_WritePin(LED_BUILTIN_PORT, LED_BUILTIN_PIN,
                    on ? LED_BUILTIN_ON_LEVEL : LED_BUILTIN_OFF_LEVEL);

  if (!on)
  {
    HAL_GPIO_WritePin(LED_EXT_PORT, LED_EXT_PIN, GPIO_PIN_RESET);
    __HAL_TIM_SET_COMPARE(&htim3, SERVO_TIM_CHANNEL, SERVO_PULSE_CENTER_US);
  }
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
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_PWM_Start(&htim3, SERVO_TIM_CHANNEL);
  System_SetOn(false);   /* старт у вимкненому стані */

  uint16_t pulseUs = SERVO_PULSE_CENTER_US;
  bool movingUp = true;
  bool ledExtOn = false;
  uint32_t lastBlinkMs = 0U;
  uint32_t lastServoMs = 0U;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    const uint32_t now = HAL_GetTick();

    /* Одне натискання — одне перемикання */
    if (Button_WasPressed(now))
    {
      System_SetOn(!systemOn);
    }

    if (systemOn)
    {
      /* Мигання зовнішнього світлодіода */
      if ((now - lastBlinkMs) >= LED_BLINK_TOGGLE_MS)
      {
        lastBlinkMs = now;
        ledExtOn = !ledExtOn;
        HAL_GPIO_WritePin(LED_EXT_PORT, LED_EXT_PIN, ledExtOn ? GPIO_PIN_SET : GPIO_PIN_RESET);
      }

      /* Плавний рух серви між крайніми положеннями */
      if ((now - lastServoMs) >= SERVO_STEP_PERIOD_MS)
      {
        lastServoMs = now;
        if (movingUp)
        {
          pulseUs += SERVO_PULSE_STEP_US;
          if (pulseUs >= SERVO_PULSE_MAX_US)
          {
            pulseUs = SERVO_PULSE_MAX_US;
            movingUp = false;
          }
        }
        else
        {
          pulseUs -= SERVO_PULSE_STEP_US;
          if (pulseUs <= SERVO_PULSE_MIN_US)
          {
            pulseUs = SERVO_PULSE_MIN_US;
            movingUp = true;
          }
        }
        __HAL_TIM_SET_COMPARE(&htim3, SERVO_TIM_CHANNEL, pulseUs);
      }
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 15;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 19999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 1500;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PA0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : PA1 */
  GPIO_InitStruct.Pin = GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
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

  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
