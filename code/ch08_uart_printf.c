/* ============================================================
 * 第8章 案例 A：printf 重定向到 USART1
 * 注意：Keil 需在 Options→Target 勾选 Use MicroLIB
 * ============================================================ */

/* 顶部头文件区 */
/* USER CODE BEGIN Includes */
#include <stdio.h>
/* USER CODE END Includes */

/* 放在 /* USER CODE BEGIN 0 */ */
/* USER CODE BEGIN 0 */
int fputc(int ch, FILE *f)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 1000);
  return ch;
}
/* USER CODE END 0 */

/* 放在 /* USER CODE BEGIN 2 */ */
/* USER CODE BEGIN 2 */
printf("Hello STM32! Clock = %lu Hz\r\n", SystemCoreClock);
/* USER CODE END 2 */
