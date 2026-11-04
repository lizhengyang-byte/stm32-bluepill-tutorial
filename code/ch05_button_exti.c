/* ============================================================
 * 第5章 案例 B：按键外部中断（每按一次翻转 LED）
 * 配置：PA0 = GPIO_EXTI0（下降沿触发），NVIC 使能 EXTI0
 * ============================================================ */

/* 放在文件底部 /* USER CODE BEGIN 4 */ 区段（中断回调） */
/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == GPIO_PIN_0)             /* 确认是 PA0 触发 */
  {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); /* 翻转 LED */
  }
}
/* USER CODE END 4 */
