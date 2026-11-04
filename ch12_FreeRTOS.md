# 第12章 FreeRTOS 实时操作系统入门

> 上一章已经实现了 STM32 与 OLED、传感器的连接，能够读取温湿度并刷新屏幕。但在主循环中，读传感器、刷屏幕、延时 2 秒这三件事是**串行排队**执行的。传感器读取快、屏幕刷新慢，串口打印若需插入则必须等待。当项目复杂度上升，要求"一边采集、一边显示、一边响应按键、一边与蓝牙通信"同时进行时，裸机 `while(1)` 结构即达到性能上限。

FreeRTOS 正是为解决这一问题而引入。本章从"为什么需要 RTOS"切入，系统梳理任务、调度、队列、信号量等核心概念，最后通过 CubeMX 一键集成，实现"两个任务 + 一个队列"的经典示例。示例代码见 [code/ch12_freertos_task.c](code/ch12_freertos_task.c)。

---

## 本章学习目标

- 理解裸机 `while(1)` 架构在复杂项目中的局限性，能够说明引入 RTOS 的必要性。
- 掌握任务（Task）的定义、四种状态（Running / Ready / Blocked / Suspended）及其切换条件。
- 理解 FreeRTOS 抢占式调度与时间片轮转的规则，能够分析给定优先级下的调度行为。
- 掌握 `vTaskDelay` 与 `HAL_Delay` 的本质区别，明确任务中必须使用 `vTaskDelay`。
- 掌握队列（Queue）、信号量（Semaphore）、互斥量（Mutex）三者的适用场景与差异。
- 理解优先级反转问题及互斥量优先级继承机制。
- 能够通过 CubeMX 完成 FreeRTOS 集成（CMSIS-V2），并编写两任务 + 队列的工程。
- 掌握中断中调用 FreeRTOS API 的硬性规则（`...FromISR` 版本 + 中断优先级阈值）。
- 能够根据项目复杂度判断是否应引入 RTOS。

---

## 12.1 裸机架构的局限性：为什么需要 RTOS

### 12.1.1 裸机 while(1) 的典型结构

前序章节编写的所有程序，骨架均如下所示：

```c
int main(void) {
    HAL_Init();
    /* 各种初始化 */
    while (1) {
        sensor_read();      // 读传感器，10ms
        oled_update();      // 刷屏幕，20ms
        uart_send();        // 串口发数据，5ms
        key_scan();         // 扫按键，5ms
        HAL_Delay(100);     // 延时 100ms
    }
}
```

该结构存在以下四类问题：

1. **所有任务串行排队**。传感器、屏幕、串口、按键依次执行，任意环节变慢，后续所有环节均被拖慢。
2. **延时为阻塞式**。`HAL_Delay(100)` 这 100 ms 内 CPU 空转，按键按下后需等待 100 ms 才被扫描到，体感明显"卡顿"。
3. **实时性无保证**。按键中断虽能抢占 CPU，但中断服务函数中不宜执行重负载操作（否则其他中断也被拖慢）；若仅做轻量处理又无法完成完整逻辑——这正是"中断只做标记，主循环轮询处理"这一传统模式的根源。
4. **代码越长越难维护**。一个 `while(1)` 中塞入 500 行代码，后续修改风险极高。

![裸机 while(1) 和 FreeRTOS 多任务的对比](images/rtos-vs-baremetal.svg)

### 12.1.2 RTOS 的核心思路：将大循环拆分为独立任务

RTOS 的核心思路是：**不再由一个 while(1) 承担所有工作，而是将每件事封装为一个独立的"任务"，由调度器决定何时运行哪个任务**。

```c
void Task_Sensor(void *arg) {
    for (;;) {
        sensor_read();
        vTaskDelay(pdMS_TO_TICKS(100));   // 每 100ms 读一次，期间让出 CPU
    }
}

void Task_OLED(void *arg) {
    for (;;) {
        oled_update();
        vTaskDelay(pdMS_TO_TICKS(50));    // 每 50ms 刷一次
    }
}

void Task_Key(void *arg) {
    for (;;) {
        key_scan();
        vTaskDelay(pdMS_TO_TICKS(20));    // 每 20ms 扫一次按键
    }
}
```

三个任务各自独立运行，调度器负责"在合适的时间切换到合适的任务"。编写任务代码时无需关心"当前是否该执行下一件事"，每个任务自行管理节奏。

### 12.1.3 为什么选择 FreeRTOS

市面 RTOS 种类繁多：RT-Thread、μC/OS、Zephyr、ThreadX 等。FreeRTOS 之所以成为入门首选，原因如下：

- **开源 + MIT 许可**，商用免费。
- **CubeMX 原生集成**，勾选即可生成工程，无需自行移植。
- **代码体积小**，内核编译后仅数 KB，F103C8T6（64 KB Flash / 20 KB RAM）完全可容纳。
- **生态成熟**，遇到问题公开资料丰富。

FreeRTOS 并非功能最强的 RTOS，但对入门者而言是"路径最短"的选择。待基础扎实后，再研究 RT-Thread 的组件化设计亦不迟。

---

## 12.2 任务：FreeRTOS 的基本调度单位

### 12.2.1 任务函数的结构

任务本质上是一个 C 函数，但必须遵守两条规则：

1. 函数永不返回（内部为 `for(;;)` 死循环）。
2. 不能永久占用 CPU——需要延时或等待事件时，应主动"让出"CPU。

```c
void vTaskBlink(void *argument)
{
    for (;;) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        vTaskDelay(pdMS_TO_TICKS(500));   // 每 500ms 翻一次 LED
    }
}
```

`argument` 为创建任务时传入的参数，不使用时可忽略。`vTaskDelay` 是 FreeRTOS 的延时函数，与裸机 `HAL_Delay` 存在本质区别，详见 12.3.2 节。

### 12.2.2 栈与优先级：任务的私有资源

创建任务时需指定两项参数：

- **栈（stack）**：任务的局部变量、函数调用、中断压栈均使用该内存区域。F103 上一般分配 128~512 个字（每字 4 字节，即 512~2048 字节）。栈过小会导致栈溢出（系统进入 HardFault），过大则浪费 RAM。
- **优先级（priority）**：数值越大优先级越高（FreeRTOS 约定，与部分系统相反）。0 为最低优先级，`configMAX_PRIORITIES - 1` 为最高优先级。

```c
xTaskCreate(vTaskBlink, "Blink", 128, NULL, 1, NULL);
//                         栈大小   参数  优先级  句柄
```

CubeMX 中上述配置均可视化：在 `Tasks and Queues` 标签中双击任务，填写名称、栈大小、优先级，代码自动生成。

![FreeRTOS 多任务调度时间轴](images/freertos-scheduling.svg)

### 12.2.3 任务的四种状态

任务并非简单的"运行 / 不运行"二元状态，而是具有四种状态：

![FreeRTOS 任务状态机：运行、就绪、阻塞、挂起](images/freertos-task-states.svg)

- **运行（Running）**：当前正在占用 CPU。同一时刻仅有一个任务处于该状态。
- **就绪（Ready）**：具备运行条件，但 CPU 被更高优先级任务占用，处于排队状态。
- **阻塞（Blocked）**：正在等待某事件——延时到期、队列有消息、信号量可用。阻塞态任务不参与调度，不占用 CPU。
- **挂起（Suspended）**：被 `vTaskSuspend()` 主动暂停，除非调用 `vTaskResume()`，否则不再运行。

该状态机是理解 FreeRTOS 的关键。任务代码中的 `vTaskDelay(500)`，本质是让任务进入阻塞状态 500 ms，调度器利用这 500 ms 切换到其他任务运行。延时到期后，任务回到就绪态，等待调度器再次切换。

---

## 12.3 调度器：高优先级抢占，同级时间片轮转

### 12.3.1 抢占式调度

FreeRTOS 默认采用**抢占式调度**（preemptive）：

- 任意时刻，只要有**高优先级任务进入就绪态**，调度器立即将当前低优先级任务切下，切换至高优先级任务运行。
- 同优先级任务按**时间片**（默认 1 ms）轮流运行，宏观上呈现"同时进行"的效果。

![优先级抢占示意：高优先级任务一就绪，立刻抢走 CPU](images/freertos-preemption.svg)

这正是"实时"的含义所在——按键响应、紧急处理等高优先级任务，不会被低优先级的刷屏、采集代码拖慢。响应延迟具有确定性。

### 12.3.2 vTaskDelay 与 HAL_Delay：本质区别

这是初学者最易踩中的问题。`HAL_Delay` 是裸机延时：依靠 SysTick 中断维护全局计数器，然后 `while` 循环死等到时间。在 FreeRTOS 工程中使用该函数会产生如下问题：

```c
void bad_task(void *arg) {
    for (;;) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        HAL_Delay(500);    // 错！
    }
}
```

`HAL_Delay(500)` 这 500 ms 内 CPU 死等，但**调度器并不知道该任务在等待**，因此不会切换到其他任务。其他本应运行的任务全部被这个 `HAL_Delay` 阻塞，系统表现为假死机。

正确写法是使用 `vTaskDelay(pdMS_TO_TICKS(500))`：

```c
void good_task(void *arg) {
    for (;;) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        vTaskDelay(pdMS_TO_TICKS(500));   // 让出 CPU，500ms 后回来
    }
}
```

`vTaskDelay` 会将任务投入阻塞队列，调度器立即切换到其他就绪任务。500 ms 后（实际为 500 个 tick），任务自动回到就绪态。

> 需特别强调：**任务中一律使用 `vTaskDelay`，禁止使用 `HAL_Delay`**。`HAL_Delay` 仅允许在 main 初始化阶段、调度器启动之前使用。

### 12.3.3 configTICK_RATE_HZ：系统节拍

FreeRTOS 的时间以 tick 为单位。`configTICK_RATE_HZ`（定义于 FreeRTOSConfig.h）默认为 1000，即 1 ms 一个 tick。`pdMS_TO_TICKS(500)` 等价于 `500 * 1000 / 1000 = 500` 个 tick。

该值并非越大越好。1000 Hz 意味着每 ms 进入一次 SysTick 中断，中断开销会占用部分 CPU；100 Hz 则精度过粗（10 ms 分辨率）。1000 是兼顾开销与精度的折中值，对 F103 已足够。

---

## 12.4 任务间通信：队列、信号量、互斥量

任务并非孤立运行，彼此之间需要传递数据与同步事件。FreeRTOS 提供以下三类机制：

![队列传数据 vs 信号量传通知](images/freertos-queue-semaphore.svg)

### 12.4.1 队列（Queue）：传递数据的流水线

队列是一个**先进先出**缓冲区。任务 A 将数据放入（`osMessageQueuePut`），任务 B 从另一端取出（`osMessageQueueGet`）。

- 传递的是**数据本身**（值拷贝），而非指针。放入一个 4 字节整数，队列中即存储一份完整拷贝。
- 队列满时，写入任务可选择"等待一段时间"（阻塞）；队列空时，读取任务也可"等待消息到来"（阻塞）。
- 由于存在阻塞机制，任务 A 放入数据后即可执行其他操作，任务 B 在有消息时自动被唤醒——无需编写"是否有消息"的轮询代码。

### 12.4.2 信号量（Semaphore）：传递通知的令牌

信号量不传递数据，仅传递"事件已发生"这一信号。最常见用法是**由中断通知任务**。

例如按键中断中不宜执行重负载操作（消抖、按键逻辑处理），可在中断中释放一个信号量；任务在另一端 `Take` 该信号量，获取后再执行按键处理。信号量类似令牌：中断 Give 一次，任务 Take 到即被唤醒。

- **二值信号量**（Binary Semaphore）：令牌仅有有 / 无两种状态，类似标志位。
- **计数信号量**（Counting Semaphore）：可累积多个令牌，例如中断中按键按下 3 次，任务唤醒后统一处理 3 次。

### 12.4.3 互斥量（Mutex）：保护共享资源

两个任务同时写入同一全局数组时，若一个任务写到一半被切走、另一个任务接着写，数据即被破坏。

互斥量类似一把锁：任务 A 操作共享资源前先 `Take` 锁，用完 `Give` 锁。期间任务 B 尝试 `Take` 时会被阻塞，等待 A 释放锁。由此保证同一时刻仅有一个任务操作共享资源。

互斥量与二值信号量外观相似，但存在关键区别：**互斥量带优先级继承机制**。当低优先级任务持有锁、高优先级任务等待锁时，低优先级任务会被临时"提升"至高优先级，使其尽快完成操作并释放锁——这是为了缓解"优先级反转"问题（详见 12.4.4）。

### 12.4.4 优先级反转：经典问题

以下场景需建立清晰认识：

- 高优先级任务 H、中优先级任务 M、低优先级任务 L。
- L 获取了互斥锁，正在修改共享资源。
- H 唤醒，需要操作同一资源，等待锁，进入阻塞。
- 此时 M 就绪（不需要锁），由于 H 在等待、L 被 M 抢占，M 开始运行。
- 结果：**M 这个"中优先级"任务反而比高优先级的 H 先完成**。这就是优先级反转。

1997 年火星探路者号在火星表面反复重启，其根因正是该问题。FreeRTOS 的互斥量带优先级继承，使 L 在 H 等待锁期间临时提升至 H 的优先级，快速释放锁，M 便无法插入运行。因此需牢记：**保护共享资源应使用互斥量，不应使用二值信号量**。

---

## 12.5 CubeMX 集成 FreeRTOS：实操步骤

![CubeMX FreeRTOS 配置界面](images/freertos-cubemx.jpg)

### 12.5.1 CubeMX 配置流程

1. 左侧 `Middleware → FREERTOS` 勾选 Activated。
2. `Interface` 选择 **CMSIS_V2**（新版 API，对应 `osThreadNew`、`osMessageQueuePut` 等接口）。
3. 切换至 `Config parameters` 标签，保持默认：
   - `Tick Rate (Hz)` = 1000
   - `Minimum Stack Size (Words)` = 128
   - 堆大小 `Total Heap Size` 默认 1536 字节左右，可支持两三个任务。
4. 切换至 `Tasks and Queues` 标签：
   - 双击默认的 `defaultTask`，将名称改为 `Task_LED`，Priority 选择 `osPriorityNormal`。
   - 点击 `Add`，新建任务，名称 `Task_Print`，优先级同样为 `osPriorityNormal`，栈大小 256（因需使用 printf）。
5. 切换至 `Queues` 标签，点击 `Add`，名称 `MsgQueue`，Queue size = 5，Item size = 4 字节（用于传递一个 uint32_t）。
6. 重新生成代码。

生成完成后，CubeMX 会在 `freertos.c` 中自动生成两个任务函数骨架与一个队列句柄。应用代码仅需填写在 `/* USER CODE BEGIN ... */` 之间。

### 12.5.2 工程文件结构

启用 FreeRTOS 后，工程新增以下内容：

- `freertos.c`：所有任务函数、队列创建均集中于此。CubeMX 自动生成，应用代码仅在 USER CODE 块内编写。
- `FreeRTOS/` 目录：内核源码（tasks.c、queue.c、list.c 等），无需修改。
- `FreeRTOSConfig.h`：所有配置开关（节拍、堆大小、优先级数），出现问题时在此调整。
- `main.c`：原有的 `while(1)` 被移除，最后一句为 `osKernelStart()`——启动调度器，此后 CPU 不再返回（由调度器接管）。

打开 `freertos.c` 可看到如下自动生成代码：

```c
void MX_FREERTOS_Init(void) {
  /* 创建任务 */
  osThreadAttr_t Task_LED_attributes = {0};
  Task_LED_attributes.stack_size = 128 * 4;
  Task_LED_attributes.priority = osPriorityNormal;
  Task_LEDHandle = osThreadNew(Task_LED, NULL, &Task_LED_attributes);

  /* 创建队列 */
  osMessageQueueAttr_t MsgQueue_attributes = {0};
  MsgQueueHandle = osMessageQueueNew(5, 4, &MsgQueue_attributes);
}
```

CubeMX 在界面上填写的栈大小、优先级、队列长度，全部转化为上述 C 调用。理解这一对应关系后，后续即便脱离 CubeMX 纯手写 FreeRTOS 工程也能上手。

### 12.5.2.1 CMSIS-RTOS v2 简介

可以注意到 API 名称为 `osThreadNew`、`osMessageQueuePut`，而非 FreeRTOS 原生的 `xTaskCreate`、`xQueueSend`。原因在于 CubeMX 默认采用 **CMSIS-RTOS v2**——ARM 制定的一层封装，将不同 RTOS（FreeRTOS、RT-Thread、RTX）的 API 统一为同一套命名。

其优势在于：应用代码更换 RTOS 时基本无需修改。劣势在于多了一层间接调用，出现问题时需知晓"osMessageQueuePut 内部实际调用的是 xQueueSend"。入门阶段使用该接口即可，待需要深入性能分析或阅读 FreeRTOS 官方文档时，再回归原生 API 亦不迟。

### 12.5.3 编写两个任务

完整代码见 [code/ch12_freertos_task.c](code/ch12_freertos_task.c)。逐行说明如下：

```c
void Task_LED(void *argument)
{
    /* USER CODE BEGIN Header_Task_LED */
    uint32_t msg = 1;
    for (;;)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);          // 翻转蓝pill的LED
        osMessageQueuePut(MsgQueueHandle, &msg, 0, 0);   // 往队列塞消息
        vTaskDelay(pdMS_TO_TICKS(1000));                 // 阻塞 1 秒
    }
    /* USER CODE END Header_Task_LED */
}
```

逐行解释：

- `HAL_GPIO_TogglePin`：每执行一次翻转一次 PC13 的 LED。
- `osMessageQueuePut(MsgQueueHandle, &msg, 0, 0)`：将 `msg`（值为 1）放入队列。第三个参数 0 为优先级，第四个参数 0 表示"队列满时不等待"——立即返回。
- `vTaskDelay(1000 tick)`：让出 CPU 一秒。

```c
void Task_Print(void *argument)
{
    /* USER CODE BEGIN Header_Task_Print */
    uint32_t rmsg;
    for (;;)
    {
        osMessageQueueGet(MsgQueueHandle, &rmsg, 0, portMAX_DELAY);
        printf("LED toggled, msg=%lu\r\n", rmsg);
    }
    /* USER CODE END Header_Task_Print */
}
```

- `osMessageQueueGet(..., portMAX_DELAY)`：从队列取消息，**没有消息则一直等待**（portMAX_DELAY = 永久阻塞）。这意味着 Task_Print 大部分时间处于阻塞状态，不占用 CPU。
- 一旦 Task_LED 放入消息，Task_Print 立即被唤醒，执行串口打印。

**预期效果**：

- LED 每 1 秒翻转一次。
- 串口每 1 秒打印一行 `LED toggled, msg=1`——并非 Task_Print 自行定时打印，而是被队列消息唤醒后打印。
- 两个任务宏观上"同时运行"，实际上由调度器在两者之间快速切换。

---

## 12.6 中断与 FreeRTOS 的配合

这是另一类常见问题。[第 5 章外部中断](ch05_EXTI中断.md)中编写的按键回调，若在 FreeRTOS 工程中直接照搬，会导致系统异常。

### 12.6.1 为什么中断中不能调用普通 API

FreeRTOS 的 API 并非可重入函数。普通任务中调用 `osMessageQueuePut` 没有问题，因为任务由调度器切换、具备上下文保护。但中断是随机插入的——若中断打断了另一个 API 调用的中间状态，内核数据结构即被破坏，系统直接进入 HardFault。

FreeRTOS 的解决方案是：**每个 API 均提供 `FromISR` 版本**，专门供中断调用。该版本在实现上更为谨慎，不会在中断中执行破坏内核状态的操作。

### 12.6.2 正确写法

```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_0)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        uint32_t evt = 1;
        osMessageQueuePutFromISR(MsgQueueHandle, &evt, 0, &xHigherPriorityTaskWoken);

        // 如果这次唤醒的任务优先级比当前任务高，触发一次上下文切换
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
```

关键三点：

1. 使用 `...FromISR` 版本。
2. 传入一个 `xHigherPriorityTaskWoken` 变量，API 会告知"是否唤醒了更高优先级的任务"。
3. 中断末尾调用 `portYIELD_FROM_ISR`，让调度器立即切换到被唤醒的高优先级任务，而非等中断退出后再切换。

### 12.6.3 中断优先级的硬性规则

这是最隐蔽的一类问题：**FreeRTOS 接管了 SysTick 与 PendSV 两个中断**，并规定——所有调用 `FromISR` API 的中断，其优先级**必须低于**某个阈值（由 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 决定）。

F103 上该值默认为 5（数字越大优先级越低，参见[第 5 章 NVIC](ch05_EXTI中断.md)的优先级编号）。含义是：外部中断、USART 中断的优先级应设为 5 或更大的数字（如 5、6、10），不得为 0~4。

原因在于：FreeRTOS 内核进入临界区（关中断）时，需要屏蔽所有"会调用 FreeRTOS API"的中断，以保证内核数据结构不被破坏。优先级 0~4 的中断不受 FreeRTOS 控制——若在其中调用了 `FromISR` API，内核正在修改队列时被打断，即会崩溃。

> 需特别提醒：按键中断若设为优先级 0（最高），按下按键即进入 HardFault。排查方法是确认中断优先级是否低于 `configMAX_SYSCALL_INTERRUPT_PRIORITY`。在 CubeMX 中将外部中断改为 5 即可解决。

---

## 12.7 常见问题与排查

**1. 任务一启动即 HardFault。**

大概率为栈过小。在 CubeMX 中将任务栈从 128 调整至 256 或 512 测试。printf 尤其消耗栈空间（浮点 printf 更甚），Task_Print 推荐分配 256。

**2. 任务中使用 `HAL_Delay`，系统表现为假死。**

替换为 `vTaskDelay(pdMS_TO_TICKS(...))`，详见 12.3.2 节。

**3. 队列句柄为 NULL。**

CubeMX 中添加队列并重新生成代码后，需注意句柄变量名是否变化（例如 `MsgQueueHandle` 被生成为 `msgQueueHandle`）。应对照 `freertos.c` 中的实际变量名修改。

**4. 中断中调用普通 API，导致系统异常。**

替换为 `FromISR` 版本，并检查中断优先级是否 ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY`。

**5. defaultTask 仍在运行。**

CubeMX 默认生成一个 `defaultTask`，若未删除，它会在后台运行（可能仅为 `vTaskDelay(osWaitForever)`，但仍占用栈空间）。应将其删除，或改造为实际使用的任务。

**6. RAM 不足。**

F103C8T6 仅有 20 KB RAM。FreeRTOS 内核 + 各任务栈 + 队列，需精确计算。一个栈 256 字（= 1 KB）的任务，3 个任务即占 3 KB，加上内核与队列，再加上 OLED 显存 1 KB、串口缓冲区……RAM 极易超限。超限表现为随机进入 HardFault，原因是栈溢出踩到了相邻内存。

**7. 中断中执行了过多操作。**

即便使用 `FromISR` 版本，中断内也不宜执行重负载操作。原则：中断仅"发送通知"，处理逻辑全部在任务中完成。中断服务时间过长，会拖慢所有其他中断与实时响应。

**8. 如何判断任务栈是否给小？**

FreeRTOS 提供实用 API：`uxTaskGetStackHighWaterMark(TaskHandle)`。该函数返回任务历史上使用过的最小剩余栈（单位为字）。若该值接近 0，说明栈即将溢出，应加大配置。调试期间可在每个任务中加入如下语句：

```c
printf("LED task stack left: %u words\r\n",
       uxTaskGetStackHighWaterMark(Task_LEDHandle));
```

运行一段时间后观察数值，即可掌握余量。经验值：运行稳定后剩余栈不应低于 20~30 字。

**9. 空闲任务（Idle Task）的作用。**

FreeRTOS 启动时自动创建一个优先级最低（0）的"空闲任务"，当所有其他任务均处于阻塞状态时，CPU 运行该任务。它主要承担两项工作：回收已删除任务的内存、执行用户的空闲钩子（若开启 `configUSE_IDLE_HOOK`）。应用代码无需主动管理，但应理解"系统看似空闲时 CPU 仍在运行"的原因——运行的即是空闲任务。

---

## 12.8 进阶实践：扩展练习

掌握"两任务 + 队列"示例后，可进行以下扩展：

**变体一：三任务 + 传感器**
新增 Task_Sensor，每 500 ms 读取一次 AHT20（参见[第 11 章](ch11_I2C_SPI与OLED.md)），读取到的温湿度通过队列发送给 Task_OLED。Task_OLED 收到消息后刷新屏幕。由此三个任务各司其职，互不阻塞。

**变体二：按键信号量**
将按键从轮询改为中断驱动。按键中断中调用 `osSemaphoreRelease`，任务中调用 `osSemaphoreAcquire`——获取信号量的任务执行按键处理（消抖、计数、切换 LED 模式）。这是 FreeRTOS 最经典的用法。

**变体三：任务通知（Task Notification）**
FreeRTOS 提供轻量机制"任务通知"，比信号量更快、更省 RAM。中断中直接调用 `vTaskNotifyGiveFromISR(TaskHandle)`，任务中调用 `ulTaskNotifyTake` 即可唤醒。无需创建队列或信号量对象。简单的"中断通知任务"场景，应优先使用该机制。

### 12.8.1 三任务完整骨架

将前述内容组合，环境监测仪 FreeRTOS 版本的骨架如下，可直接套用：

```c
/* Task_Sensor: 每 500ms 读一次 AHT20，结果通过队列发给显示任务 */
void Task_Sensor(void *arg) {
    for (;;) {
        float temp, humi;
        aht20_read(&temp, &humi);
        sensor_msg_t msg = {.temp = temp, .humi = humi};
        osMessageQueuePut(SensorQueueHandle, &msg, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* Task_Display: 收到传感器消息就刷一次 OLED，平时阻塞 */
void Task_Display(void *arg) {
    sensor_msg_t msg;
    for (;;) {
        osMessageQueueGet(SensorQueueHandle, &msg, 0, portMAX_DELAY);
        oled_show_temp_humi(msg.temp, msg.humi);
    }
}

/* Task_Key: 等按键信号量，按一下切换显示单位（℃ / ℉） */
void Task_Key(void *arg) {
    for (;;) {
        osSemaphoreAcquire(KeySemHandle, osWaitForever);
        display_unit = (display_unit == CELSIUS) ? FAHRENHEIT : CELSIUS;
        /* 消抖 */
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
```

该结构的优势在于：传感器读取变慢不影响显示（队列缓冲）；OLED 刷新 80 ms 也不阻塞传感器（独立任务）；按键随时按下随时响应（高优先级）。每个任务代码简短，修改时不会波及其他部分。

### 12.8.2 优先级排序原则

通用的优先级分配参考：

- 最高优先级：紧急处理（故障、停机、电机 PWM 紧急输出）。
- 高优先级：按键、通信接收（要求快速响应）。
- 中优先级：传感器采集（周期固定）。
- 低优先级：OLED 刷屏、日志打印（允许较慢）。

刷屏任务之所以放在最低优先级，原因在于其耗时最长（I2C 刷新一次约 80 ms）。若优先级过高，会阻塞其他所有任务。放在低优先级时，CPU 空闲则刷新、繁忙则暂缓——数据从队列中获取，稍晚显示不影响功能。

---

## 12.9 何时引入 RTOS，何时裸机足够

需避免为简单项目强行引入 RTOS。以下给出判断标准：

| 项目特征 | 建议 |
|---|---|
| 仅点亮 LED、读取按键、偶尔串口打印 | 裸机，RTOS 属过度设计 |
| 一个 main 循环中超过 3 件事，且彼此需要等待 | 引入 RTOS |
| 需同时执行"数据采集 + 刷屏 + 按键响应 + 通信" | 引入 RTOS |
| 项目代码超过 500 行，维护困难 | 引入 RTOS，拆分为任务可显著改善结构 |
| 对实时性有要求（按键响应 < 10 ms） | 引入 RTOS，依靠优先级保证 |

裸机并非过时方案——简单项目中裸机代码更短、更可控、调试更快。RTOS 是复杂项目的工具，而非目标本身。

---

## 12.10 本章小结

- RTOS 解决的是裸机 `while(1)` 串行、阻塞、难维护的问题。
- 任务 = 独立函数 + 独立栈 + 优先级，函数体为永久死循环。
- 调度器采用高优先级抢占、同级时间片轮转；任务在 Running / Ready / Blocked / Suspended 四种状态间切换。
- 队列传递数据、信号量传递通知、互斥量保护共享资源（带优先级继承，不应以信号量代替互斥量）。
- CubeMX 一键集成 FreeRTOS（CMSIS-V2），任务函数在 `freertos.c` 中编写。
- 任务内必须使用 `vTaskDelay`，禁止使用 `HAL_Delay`。
- 中断中使用 `...FromISR` 版本 API，且中断优先级必须低于 `configMAX_SYSCALL_INTERRUPT_PRIORITY`。
- 栈过小会导致 HardFault；RAM 使用需精确计算。

## 12.11 练习题

1. `vTaskDelay(1000)` 与 `HAL_Delay(1000)` 在 FreeRTOS 工程中的行为有何本质区别？请从调度器视角解释。
2. 三个任务优先级分别为 1、2、3。优先级 3 的任务处于 `vTaskDelay` 阻塞，优先级 1 的任务正在运行，此时优先级 2 的任务进入就绪态，调度器将如何切换？请写出完整过程。
3. 队列与互斥量有何区别？保护一个"两个任务同时写入的全局数组"，应选用哪一种？为什么？
4. 为什么在中断中调用 `osMessageQueuePut`（不带 FromISR）会导致系统异常？请从内核数据结构保护角度解释。
5. F103C8T6 仅有 20 KB RAM，若需运行 4 个任务，栈各应分配多大？请给出计算过程与余量考量。

至此，[STM32 从点灯到 FreeRTOS](ch01_硬件认知.md)的主线内容已完成。遗留问题、参考资料、后续学习方向，均整理于[附录](appendix.md)中。

---

<!-- ===== 第12章 完 ===== -->
