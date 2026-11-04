/* ============================================================
 * 第10章 案例：串口 DMA 接收一帧数据
 * 配置：USART1 + USART1_RX DMA（Circular）
 * ============================================================ */

/* 顶部全局变量 */
/* USER CODE BEGIN PV */
uint8_t rx_buf[32];
/* USER CODE END PV */

/* 放在 /* USER CODE BEGIN 2 */ */
/* USER CODE BEGIN 2 */
HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buf, sizeof(rx_buf));
/* USER CODE END 2 */

/* 放在 /* USER CODE BEGIN 4 */（DMA 接收事件回调） */
/* USER CODE BEGIN 4 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if (huart->Instance == USART1)
  {
    printf("Received %u bytes: %s\r\n", Size, rx_buf);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buf, sizeof(rx_buf));
  }
}
/* USER CODE END 4 */
