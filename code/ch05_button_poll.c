/* ============================================================
 * 第5章 案例 A：按键轮询（按下点亮，松开熄灭）
 * 接线：按键一端接 PA0，另一端接 GND；PA0 内部上拉
 * 逻辑：按下→PA0 读低→点亮；松开→读高→熄灭
 * ============================================================ */

/* 放在 /* USER CODE BEGIN 2 */ */
/* USER CODE BEGIN 2 */
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);  /* 初始熄灭 */
/* USER CODE END 2 */

/* 放在 while(1) 内 USER CODE 区 */
/* USER CODE END WHILE */
if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET)
{
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); /* 点亮 */
}
else
{
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);   /* 熄灭 */
}
/* USER CODE BEGIN 3 */
