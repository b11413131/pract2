/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : UART Brute-Force Detector + Dynamic Lockdown (STM32G474RE)
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum { SEC_NORMAL = 0, SEC_LOCKDOWN } SecState_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 外接紅燈 PC9：Lockdown 暴閃 */
#define LEDR_PORT           GPIOC
#define LEDR_PIN            GPIO_PIN_9
/* 外接綠燈 PC8：已解鎖 */
#define LEDG_PORT           GPIOC
#define LEDG_PIN            GPIO_PIN_8
/* 板上內建綠燈 PA5：Lockdown 時同步暴閃，沒接外部 LED 也看得到 */
#define LEDB_PORT           GPIOA
#define LEDB_PIN            GPIO_PIN_5

#define SEC_UART            (&hlpuart1)
#define MAX_FAIL            3
#define LOCKDOWN_MS         10000U
#define BLINK_MS            50U
#define RX_BUF_LEN          32
#define PASSWORD_CMD        "AUTH 1234"
#define LOCK_CMD            "LOCK"
#define DEFAULT_PWD         "1234"
#define PWD_MIN             4
#define PWD_MAX             16
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef hlpuart1;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static SecState_t sec_state = SEC_NORMAL;
static uint8_t  rx_byte;
static char     rx_buf[RX_BUF_LEN];
static uint8_t  rx_idx = 0;
static volatile uint8_t line_ready = 0;
static volatile uint8_t overflow   = 0;

static char     password[PWD_MAX + 1] = DEFAULT_PWD;   // changeable password (RAM)
static uint8_t  authenticated = 0;   // 0 = 鎖定，1 = 已解鎖
static uint8_t  fail_count = 0;
static uint32_t lock_start = 0, last_blink = 0, last_sec = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void uart_print(const char *s)
{
    HAL_UART_Transmit(SEC_UART, (uint8_t *)s, strlen(s), 100);
}

/* 紅燈（外接 PC9 + 板上 PA5 同步） */
static void red_set(GPIO_PinState s)
{
    HAL_GPIO_WritePin(LEDR_PORT, LEDR_PIN, s);
    HAL_GPIO_WritePin(LEDB_PORT, LEDB_PIN, s);
}

static void red_toggle(void)
{
    HAL_GPIO_TogglePin(LEDR_PORT, LEDR_PIN);
    HAL_GPIO_TogglePin(LEDB_PORT, LEDB_PIN);
}

/* 每收到 1 byte 進入此函式 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == SEC_UART)
    {
        if (rx_byte == '\r' || rx_byte == '\n')
        {
            if (rx_idx > 0 && !line_ready) {   // 忽略空行（避免 CR+LF 被算兩次）
                rx_buf[rx_idx] = '\0';
                line_ready = 1;
            }
        }
        else if (!line_ready)
        {
            if (rx_idx < RX_BUF_LEN - 1) rx_buf[rx_idx++] = rx_byte;
            else                         overflow = 1;   // 超長輸入視為攻擊
        }
        HAL_UART_Receive_IT(SEC_UART, &rx_byte, 1);
    }
}

/* 接收錯誤（例如 Overrun）時清旗標並重新開啟接收 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == SEC_UART)
    {
        __HAL_UART_CLEAR_OREFLAG(SEC_UART);
        __HAL_UART_CLEAR_FEFLAG(SEC_UART);
        __HAL_UART_CLEAR_NEFLAG(SEC_UART);
        if (sec_state == SEC_NORMAL) {
            HAL_UART_Receive_IT(SEC_UART, &rx_byte, 1);
        }
    }
}

static void enter_lockdown(void)
{
    sec_state = SEC_LOCKDOWN;
    authenticated = 0;
    HAL_GPIO_WritePin(LEDG_PORT, LEDG_PIN, GPIO_PIN_RESET);   // 綠燈滅：強制上鎖
    HAL_UART_AbortReceive_IT(SEC_UART);                        // 徹底關閉接收
    uart_print("\r\n[SECURITY ALERT] Brute-force detected! LOCKDOWN 10s\r\n");
    lock_start = last_blink = last_sec = HAL_GetTick();
}

static void exit_lockdown(void)
{
    red_set(GPIO_PIN_RESET);
    __HAL_UART_CLEAR_OREFLAG(SEC_UART);                         // 清除鎖定期間累積的錯誤旗標
    __HAL_UART_SEND_REQ(SEC_UART, UART_RXDATA_FLUSH_REQUEST);   // 清空 RX 暫存
    rx_idx = 0; line_ready = 0; overflow = 0; fail_count = 0;
    sec_state = SEC_NORMAL;
    HAL_UART_Receive_IT(SEC_UART, &rx_byte, 1);                 // 重新開啟接收
    uart_print("[SECURITY RECOVER]\r\n");
}

static uint8_t pwd_equal(const char *a, const char *b)
{
    uint8_t diff = (strlen(a) != strlen(b));
    for (uint8_t i = 0; i < RX_BUF_LEN; i++) {      /* constant-time compare */
        char x = (i < strlen(a)) ? a[i] : 0;
        char y = (i < strlen(b)) ? b[i] : 0;
        diff |= (uint8_t)(x ^ y);
    }
    return diff == 0;
}

static void fail_attempt(void)
{
    char msg[64];
    fail_count++;
    snprintf(msg, sizeof(msg), "[FAIL] Invalid command (%u/%u)\r\n", fail_count, MAX_FAIL);
    uart_print(msg);
    if (fail_count >= MAX_FAIL) enter_lockdown();
}

static void process_line(void)
{
    if (overflow)
    {
        fail_attempt();
    }
    else if (strncmp(rx_buf, "AUTH ", 5) == 0 && pwd_equal(rx_buf + 5, password))
    {
        fail_count = 0;
        authenticated = 1;
        HAL_GPIO_WritePin(LEDG_PORT, LEDG_PIN, GPIO_PIN_SET);
        uart_print("[OK] Access granted (UNLOCKED)\r\n");
    }
    else if (strcmp(rx_buf, LOCK_CMD) == 0)
    {
        authenticated = 0;
        fail_count = 0;
        HAL_GPIO_WritePin(LEDG_PORT, LEDG_PIN, GPIO_PIN_RESET);
        uart_print("[LOCKED]\r\n");
    }
    else if (strncmp(rx_buf, "SETPWD ", 7) == 0)
    {
        if (!authenticated) {                       /* not logged in: counts as a failure */
            uart_print("[DENY] Login first\r\n");
            fail_attempt();
        } else {
            size_t len = strlen(rx_buf + 7);
            if (len < PWD_MIN || len > PWD_MAX) {
                uart_print("[ERROR] Password must be 4-16 chars\r\n");
            } else {
                strcpy(password, rx_buf + 7);
                uart_print("[OK] Password changed\r\n");
            }
        }
    }
    else if (strcmp(rx_buf, "RESETPWD") == 0)
    {
        if (!authenticated) {
            uart_print("[DENY] Login first\r\n");
            fail_attempt();
        } else {
            strcpy(password, DEFAULT_PWD);
            authenticated = 0;
            HAL_GPIO_WritePin(LEDG_PORT, LEDG_PIN, GPIO_PIN_RESET);
            uart_print("[OK] Password reset to default (LOCKED)\r\n");
        }
    }
    else
    {
        fail_attempt();
    }

    rx_idx = 0; line_ready = 0; overflow = 0;
}

static void Security_Task(void)
{
    uint32_t now = HAL_GetTick();

    if (sec_state == SEC_NORMAL)
    {
        if (line_ready || overflow) process_line();
    }
    else  // SEC_LOCKDOWN
    {
        if (now - last_blink >= BLINK_MS) {            // 高頻暴閃
            last_blink = now;
            red_toggle();
        }
        if (now - last_sec >= 1000U) {                 // 每秒倒數顯示
            last_sec += 1000U;
            char msg[32];
            uint32_t elapsed = now - lock_start;
            uint32_t remain = (elapsed >= LOCKDOWN_MS) ? 0U : (LOCKDOWN_MS - elapsed) / 1000U;
            snprintf(msg, sizeof(msg), "[LOCKDOWN] %lus\r\n", (unsigned long)remain);
            uart_print(msg);
        }
        if (now - lock_start >= LOCKDOWN_MS) exit_lockdown();
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
  MX_LPUART1_UART_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_NVIC_SetPriority(LPUART1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(LPUART1_IRQn);

  uart_print("=== UART Security FSM Ready. Type: AUTH 1234 ===\r\n");
  if (HAL_UART_Receive_IT(SEC_UART, &rx_byte, 1) != HAL_OK) {
      uart_print("RX start failed\r\n");
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    Security_Task();
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

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART1_Init 2 */

  /* USER CODE END LPUART1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

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
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8|GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC8 PC9 */
  GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* 板上內建 LED PA5（Lockdown 時同步暴閃） */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
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
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
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
