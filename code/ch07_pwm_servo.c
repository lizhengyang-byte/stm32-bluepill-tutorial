/* ============================================================
 * 第7章 案例 B：SG90 舵机控制
 * 配置：PSC=71, ARR=19999 → 20ms / 50Hz
 * 占空比：0.5ms→0°(CCR=500)，1.5ms→90°(CCR=1500)，2.5ms→180°(CCR=2500)
 * 接线：SG90 红线→5V，棕线→GND，橙线→PA0
 * ============================================================ */

/* 放在 /* USER CODE BEGIN 2 */ */
/* USER CODE BEGIN 2 */
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 1500);  /* 转到 90° */
/* USER CODE END 2 */

/* 如需连续扫角，放在 while(1) USER CODE 区 */
/* USER CODE END WHILE */
for (uint16_t pos = 500; pos <= 2500; pos += 50)
{
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pos);
  HAL_Delay(20);
}
for (uint16_t pos = 2500; pos >= 500; pos -= 50)
{
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pos);
  HAL_Delay(20);
}
/* USER CODE BEGIN 3 */
