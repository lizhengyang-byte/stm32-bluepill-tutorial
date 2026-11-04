/* ============================================================
 * 第11章 案例：I2C 驱动 0.96 寸 SSD1306 OLED
 * 接线：OLED GND→GND, VCC→3V3, SCL→PB6, SDA→PB7
 * 前置：将 ssd1306.c/.h 驱动加入工程
 * ============================================================ */

/* 顶部头文件 */
/* USER CODE BEGIN Includes */
#include "ssd1306.h"
/* USER CODE END Includes */

/* 放在 /* USER CODE BEGIN 2 */ */
/* USER CODE BEGIN 2 */
ssd1306_Init();
ssd1306_Fill(Black);
ssd1306_SetCursor(10, 0);
ssd1306_WriteString("Hello STM32!", Font_7x10, White);
ssd1306_UpdateScreen();
/* USER CODE END 2 */
