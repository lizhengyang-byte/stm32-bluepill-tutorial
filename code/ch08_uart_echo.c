/* ============================================================
 * 第8章 案例 B：串口中断接收，回显收到的字节
 * 配置：USART1，开全局中断
 * ============================================================ */

/* 顶部全局变量 */
/* USER CODE BEGIN PV */
uint8_t rx_byte = 0;
/* USER CODE END PV */

/* 放在 /* USER CODE BEGIN 2 */ */
/* USER CODE BEGIN 2 */
HAL_UART_Receive_IT(&huart1, &rx_byte, 1);   /* 启动中断接收 1 字节 */
/* USER CODE END 2 */

/* 放在 /* USER CODE BEGIN 4 */（接收完成回调） */
/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    HAL_UART_Transmit(&huart1, &rx_byte, 1, 100); /* 回显 */
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);    /* 重新开启 */
  }
}
/* USER CODE END 4 */
