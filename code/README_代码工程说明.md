# STM32 讲义配套代码工程

> 本目录包含讲义各章核心案例的**代码片段**，均基于 **STM32CubeMX + HAL 库**，可在对应章节所述的三 IDE（Keil MDK / STM32CubeIDE / PlatformIO）工程中编译运行。
>
> **使用方式**：每章的代码都标注了它应粘贴到 CubeMX 生成工程 `main.c` 的哪个 `USER CODE` 区段。请先按讲义第 2 章用 CubeMX 生成对应工程，再粘贴代码。

## 工程文件说明

| 文件 | 对应章节 | 演示内容 |
|---|---|---|
| `ch03_led_blink.c` | 第3章 | GPIO 输出，LED 闪烁 |
| `ch05_button_poll.c` | 第5章 | 按键轮询点亮 LED |
| `ch05_button_exti.c` | 第5章 | 按键外部中断 EXTI |
| `ch06_timer_it.c` | 第6章 | TIM2 定时中断 |
| `ch07_pwm_breath.c` | 第7章 | PWM 呼吸灯 |
| `ch07_pwm_servo.c` | 第7章 | PWM 舵机控制 |
| `ch08_uart_printf.c` | 第8章 | printf 重定向 |
| `ch08_uart_echo.c` | 第8章 | 串口中断接收回显 |
| `ch09_adc_single.c` | 第9章 | ADC 单通道采集 |
| `ch09_adc_multi_dma.c` | 第9章 | ADC 多通道 DMA |
| `ch10_uart_dma.c` | 第10章 | 串口 DMA 接收 |
| `ch11_oled_i2c.c` | 第11章 | I2C 驱动 OLED |
| `ch12_freertos_task.c` | 第12章 | FreeRTOS 多任务 + 队列 |

每个 `.c` 文件都是**可直接插入对应 `main.c` USER CODE 区**的完整代码块，含必要注释。以下代码可在对应 CubeMX 工程中编译。

---

## ch03_led_blink.c —— 第3章 LED 闪烁（GPIO 输出）

**CubeMX 配置**：PC13 设为 GPIO_Output；SYS 开 Serial Wire；时钟 72MHz。

```c
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
```

---

## ch05_button_poll.c —— 第5章 按键轮询

**CubeMX 配置**：PC13=GPIO_Output；PA0=GPIO_Input，Pull-up。

```c
/* ============================================================
 * 第5章 案例 A：按键轮询（按下点亮，松开熄灭）
 * 接线：按键一端接 PA0，另一端接 GND；PA0 内部上拉
 * 逻辑：按下→PA0 读低→点亮；松开→读高→熄灭
 * ============================================================ */

/* 放在 /* USER CODE BEGIN 2 */ */
/* USER CODE BEGIN 2 */
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);  /* 初始熄灭 */
/* USER CODE END 2 */

/* 放在 while(1) 内 USER CODE 区 */
/* USER CODE END WHILE */
if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET)
{
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); /* 点亮 */
}
else
{
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);   /* 熄灭 */
}
/* USER CODE BEGIN 3 */
```

---

## ch05_button_exti.c —— 第5章 按键外部中断 EXTI

**CubeMX 配置**：PC13=GPIO_Output；PA0=GPIO_EXTI0，Falling edge；NVIC 开 EXTI0。

```c
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
```

---

## ch06_timer_it.c —— 第6章 TIM2 定时中断

**CubeMX 配置**：TIM2 Activated，Prescaler=71，Counter Period=9999；NVIC 勾选 TIM2 global interrupt。

```c
/* ============================================================
 * 第6章 案例：TIM2 定时中断，LED 每秒翻转一次
 * 配置：PSC=71（1MHz 计数），ARR=9999（10ms 溢出）
 *       用计数 100 次 × 10ms = 1s
 * ============================================================ */

/* 顶部全局变量区 */
/* USER CODE BEGIN PV */
volatile uint32_t tim_counter = 0;
/* USER CODE END PV */

/* 放在 /* USER CODE BEGIN 2 */ */
/* USER CODE BEGIN 2 */
HAL_TIM_Base_Start_IT(&htim2);   /* 启动定时中断 */
/* USER CODE END 2 */

/* 放在 /* USER CODE BEGIN 4 */（定时器溢出回调） */
/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
    tim_counter++;
    if (tim_counter >= 100)            /* 100×10ms = 1s */
    {
      tim_counter = 0;
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
  }
}
/* USER CODE END 4 */
```

---

## ch07_pwm_breath.c —— 第7章 PWM 呼吸灯

**CubeMX 配置**：TIM2 Activated，PWM Generation CH1；Prescaler=71，Counter Period=999（→1kHz），Pulse=0；PA0=TIM2_CH1。

```c
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
```

---

## ch07_pwm_servo.c —— 第7章 PWM 舵机控制（SG90）

**CubeMX 配置**：TIM2 CH1，PSC=71，Counter Period=19999（→50Hz）；PA0=TIM2_CH1。

```c
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
```

---

## ch08_uart_printf.c —— 第8章 printf 重定向

**CubeMX 配置**：USART1 Activated，Baud 115200，8N1；PA9=TX，PA10=RX。

```c
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
```

---

## ch08_uart_echo.c —— 第8章 串口中断接收回显

**CubeMX 配置**：USART1 Activated，开 USART1 global interrupt。

```c
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
```

---

## ch09_adc_single.c —— 第9章 ADC 单通道采集

**CubeMX 配置**：ADC1 Activated；PA0=ADC1_IN0（Analog）。

```c
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
```

---

## ch09_adc_multi_dma.c —— 第9章 ADC 多通道 + DMA

**CubeMX 配置**：ADC1，Scan=Enable，Continuous=Enable，Number of Conversion=2；PA0=ADC1_IN0，PA1=ADC1_IN1；DMA Settings 加 ADC1 DMA。

```c
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
```

---

## ch10_uart_dma.c —— 第10章 串口 DMA 接收

**CubeMX 配置**：USART1 Activated + 开中断；DMA Settings 加 USART1_RX，Mode=Circular，Byte。

```c
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
```

---

## ch11_oled_i2c.c —— 第11章 I2C 驱动 SSD1306 OLED

**CubeMX 配置**：I2C1 Activated；PB6=SCL，PB7=SDA。需引入 ssd1306 驱动（见讲义 11.3）。

```c
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
```

---

## ch12_freertos_task.c —— 第12章 FreeRTOS 多任务 + 队列

**CubeMX 配置**：Middleware→FREERTOS Activated，Interface=CMSIS_V2；Task_LED + Task_Print 两个任务，Priority=osPriorityNormal；添加队列 MsgQueue。

```c
/* ============================================================
 * 第12章 案例：FreeRTOS 两个任务 + 队列通信
 * 配置：CubeMX 集成 FreeRTOS（CMSIS-V2）
 *       Task_LED（每 1s 翻转 LED）→ 队列 → Task_Print（打印）
 * ============================================================ */

/* 在 CubeMX 生成的任务函数 Task_LED 体内（/* USER CODE BEGIN 5 */） */
/* USER CODE BEGIN Header_Task_LED */
void Task_LED(void *argument)
{
  /* USER CODE BEGIN 5 */
  uint32_t msg = 1;
  for (;;)
  {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);          /* 翻转 LED */
    osMessageQueuePut(MsgQueueHandle, &msg, 0, 0);   /* 发消息 */
    vTaskDelay(pdMS_TO_TICKS(1000));                 /* 延时让出CPU */
  }
  /* USER CODE END 5 */
}
/* USER CODE END Header_Task_LED */

/* 在 Task_Print 任务函数体内 */
void Task_Print(void *argument)
{
  /* USER CODE BEGIN 5 */
  uint32_t rmsg;
  for (;;)
  {
    osMessageQueueGet(MsgQueueHandle, &rmsg, 0, portMAX_DELAY); /* 阻塞等 */
    printf("LED toggled, msg=%lu\r\n", rmsg);
  }
  /* USER CODE END 5 */
}
```

---

> **提示**：以上代码片段需粘贴到 CubeMX 生成的对应工程中。若某章节的 CubeMX 配置项与本文件注释不符，以讲义正文的详细配置步骤为准。
