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
