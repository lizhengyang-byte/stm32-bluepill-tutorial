/* ============================================================
 * 第6章 案例：TIM2 定时中断，LED 每秒翻转一次
 * 配置：PSC=71（1MHz 计数），ARR=9999（10ms 溢出）
 *       用计数 100 次 × 10ms = 1s
 * ============================================================ */

/* 顶部全局变量区 */
/* USER CODE BEGIN PV */
volatile uint32_t tim_counter = 0;
/* USER CODE END PV */

/* 放在 /* USER CODE BEGIN 2 */ */
/* USER CODE BEGIN 2 */
HAL_TIM_Base_Start_IT(&htim2);   /* 启动定时中断 */
/* USER CODE END 2 */

/* 放在 /* USER CODE BEGIN 4 */（定时器溢出回调） */
/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
    tim_counter++;
    if (tim_counter >= 100)            /* 100×10ms = 1s */
    {
      tim_counter = 0;
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
  }
}
/* USER CODE END 4 */
