/* ============================================================
 * 第7章 案例 A：PWM 呼吸灯（LED 亮度渐变）
 * 配置：TIM2 CH1 (PA0)，PSC=71, ARR=999 → 1kHz
 * 接线：LED 接 PA0 + 电阻 + GND（高电平点亮接法）
 * ============================================================ */

/* 顶部全局变量 */
/* USER CODE BEGIN PV */
uint16_t duty = 0;
uint8_t  dir  = 1;
/* USER CODE END PV */

/* 放在 /* USER CODE BEGIN 2 */ */
/* USER CODE BEGIN 2 */
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);   /* 启动 PWM */
/* USER CODE END 2 */

/* 放在 while(1) USER CODE 区 */
/* USER CODE END WHILE */
if (dir)
{
  duty += 5;
  if (duty >= 999) dir = 0;
}
else
{
  duty -= 5;
  if (duty <= 0) dir = 1;
}
__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty);
HAL_Delay(10);
/* USER CODE BEGIN 3 */
