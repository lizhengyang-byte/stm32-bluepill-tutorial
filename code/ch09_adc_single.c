/* ============================================================
 * 第9章 案例 A：ADC 单通道采集电位器电压，串口打印
 * 配置：PA0=ADC1_IN0，12bit，单次转换
 * 接线：电位器滑动端接 PA0
 * ============================================================ */

/* 顶部全局变量 */
/* USER CODE BEGIN PV */
uint16_t adc_val = 0;
float voltage = 0.0f;
/* USER CODE END PV */

/* 放在 while(1) USER CODE 区 */
/* USER CODE END WHILE */
HAL_ADC_Start(&hadc1);
HAL_ADC_PollForConversion(&hadc1, 100);
adc_val = HAL_ADC_GetValue(&hadc1);
voltage = adc_val * 3.3f / 4095.0f;
printf("ADC=%u, %.2f V\r\n", adc_val, voltage);
HAL_Delay(500);
/* USER CODE BEGIN 3 */
