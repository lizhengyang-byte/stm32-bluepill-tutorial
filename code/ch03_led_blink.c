/* ============================================================
 * 第3章 案例：PC13 板载 LED 闪烁（低电平点亮）
 * 需要：CubeMX 配置 PC13 = GPIO_Output
 * ============================================================ */

/* 放在 main() 里 /* USER CODE BEGIN 2 */ 区段 */
/* USER CODE BEGIN 2 */
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  /* 先点亮 */
/* USER CODE END 2 */

/* 放在 while(1) 循环的 /* USER CODE END WHILE */ 之后、
 * /* USER CODE BEGIN 3 */ 之前 */
/* USER CODE END WHILE */
HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);   /* 翻转电平 */
HAL_Delay(500);                            /* 延时 500ms */
/* USER CODE BEGIN 3 */
