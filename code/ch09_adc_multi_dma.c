/* ============================================================
 * 第9章 案例 B：ADC 多通道 DMA 采集
 * 配置：PA0/PA1 两通道，扫描+连续+DMA
 * ============================================================ */

/* 顶部全局变量 */
/* USER CODE BEGIN PV */
uint32_t adc_buf[2];
/* USER CODE END PV */

/* 放在 /* USER CODE BEGIN 2 */ */
/* USER CODE BEGIN 2 */
HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buf, 2);   /* DMA 采集 2 通道 */
/* USER CODE END 2 */

/* 放在 while(1) USER CODE 区 */
/* USER CODE END WHILE */
printf("CH0=%lu (%.2fV)  CH1=%lu (%.2fV)\r\n",
       adc_buf[0], adc_buf[0]*3.3f/4095.0f,
       adc_buf[1], adc_buf[1]*3.3f/4095.0f);
HAL_Delay(500);
/* USER CODE BEGIN 3 */
