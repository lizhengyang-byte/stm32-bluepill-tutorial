# 第10章 DMA 数据传输：解放 CPU 的硬件搬运机制

## 本章学习目标

学完本章后，应当能够：

- 解释 DMA（Direct Memory Access，直接存储器访问）的基本概念，并说明其相比 CPU 逐字节搬运的优势。
- 描述 STM32F103 总线架构与总线仲裁器的工作原理。
- 查阅 DMA1 通道映射表，正确判断 ADC1、USART1_RX/TX 等外设在 DMA1 上的通道分配，并识别通道冲突。
- 掌握 DMA 五大传输要素：源地址、目的地址、数据长度、数据宽度、传输模式（Normal/Circular）。
- 使用 `HAL_UARTEx_ReceiveToIdle_DMA` 实现串口不定长数据接收。
- 使用 `HAL_ADC_Start_DMA` 实现 ADC 多通道连续采样。
- 配置 DMA 半传输/全传输中断，实现双缓冲采集。
- 根据数据量与频率合理判断何时使用 DMA、何时使用阻塞或中断方式。

---

## 10.1 没有 DMA 的世界：CPU 搬运数据的代价

从外设读取一个字节，CPU 实际需要完成以下操作：

```c
while (!(USART1->SR & USART_SR_RXNE));   // 1. 轮询标志
uint8_t b = USART1->DR;                  // 2. 从 DR 读一个字节
buf[idx++] = b;                          // 3. 写进内存数组
if (idx >= N) process();                 // 4. 处理
```

每接收一个字节，CPU 至少执行 5~10 条指令，并占用寄存器与流水线资源。若采用中断接收方式，每字节还需额外执行压栈、出栈操作，开销更大。

以 USART1 115200 波特率为例，每秒约可接收 11520 个字节。假定每次中断占用 200 个时钟周期（压栈 + 读 DR + 写内存 + 出栈），则 CPU 占用率为 11520 × 200 / 72000000 ≈ 3.2%。单看这一比例似乎不高，但当系统同时开启 ADC 1 kHz 采样、SPI 驱动 OLED 刷屏、PWM 控制舵机等任务时，占用率将迅速上升。更关键的是，**中断是随机插入的**，主循环的实时性因此难以保证。

DMA（Direct Memory Access，直接存储器访问）正是为解决上述问题而引入的硬件机制。它是 Cortex-M3 内核旁一个**独立的硬件搬运单元**，CPU 只需告知其"从哪里搬到哪里、搬多长、搬完如何通知"，之后 DMA 即可独立运行。搬运期间 CPU 完全空闲；搬完 N 个字节后，DMA 向 CPU 发出一次中断，CPU 才被唤醒进行后续处理。

```
无 DMA：CPU 亲自搬运，每字节都要管 → 慢、占资源
有 DMA：DMA 硬件搬运，CPU 只在搬完时被通知一次 → 快、解放 CPU
```

![DMA 数据搬运原理：无 DMA 与有 DMA 对比](images/dma-transfer.svg)

可将其类比为前台服务体系：没有 DMA 时，CPU 如同一名前台接待，每位客人都需亲自引导入座、倒水、传菜；引入 DMA 后，相当于聘用了一名专职服务员（DMA 控制器），前台仅负责接待与结账，中间传菜工作全部由服务员完成。

---

## 10.2 DMA 在 F103 中的位置与总线结构

STM32F103C8T6 的内核为 Cortex-M3，内部包含三条总线：I-bus（取指）、D-bus（取数据）、System 总线（访问外设）。DMA 挂接在 System 总线上，与 CPU 共享总线带宽。

![STM32F103 总线架构](images/stm32-bus-matrix.jpg)

此处的关键概念是**总线仲裁器（Bus Arbiter）**。当 CPU 与 DMA 同时访问 SRAM 或外设时，由仲裁器决定总线使用权的归属。F103 的策略是 CPU 优先级略高，但 DMA 仍可获得总线周期。因此 DMA 搬运并非"CPU 完全停止"，而是"CPU 与 DMA 分时复用总线"，DMA 大约可占用 20%~40% 的总线带宽。

F103 片内包含两个 DMA 控制器：

- **DMA1**：挂接在 APB1 总线上，共 7 个通道，仅能访问 SRAM 与 APB1 上的外设，**不支持内存到内存传输**。
- **DMA2**：挂接在 AHB 总线上，共 5 个通道，支持内存到内存传输，可访问全部外设。

Blue Pill（C8T6）仅引出 DMA1，DMA2 在该封装下不可用（需 F103RCT6 及以上型号才完整）。因此在 Blue Pill 上学习 DMA，主要实验对象为 DMA1。

---

## 10.3 DMA1 通道映射

初学者配置 DMA 时常见的疑问是：为何 ADC1 自动绑定到 DMA1 Channel 1？为何 USART1_RX 不能选 Channel 1？其原因在于：**F103 的 DMA 通道与外设之间是硬件固定映射，不能任意组合。**

![STM32F103 DMA1 通道映射表](images/dma1-channel-map.svg)

工程中最常用的映射如表 10-1 所示。

| 通道 | 最常见外设 | 用途 |
|---|---|---|
| DMA1_CH1 | ADC1 | ADC 采样自动搬运 |
| DMA1_CH4 | USART1_TX | 串口发送 |
| DMA1_CH5 | USART1_RX | 串口接收 |
| DMA1_CH6 | USART2_RX | 串口 2 接收 |
| DMA1_CH7 | USART2_TX | 串口 2 发送 |

CubeMX 会自动为外设选择对应通道，无需手工填写。但应理解以下两点：

- 若希望 ADC1 与 USART1_RX 同时使用 DMA，二者并不冲突，因为 CH1 与 CH5 相互独立；
- 若试图让 USART1_TX 与 TIM4_CH4 同时使用 CH4，则会发生通道冲突，CubeMX 会以红字报错。

---

## 10.4 DMA 五大传输要素

配置一个 DMA 通道，本质上是回答以下五个问题：

1. **从哪里搬（源地址）**：是外设寄存器（例如 `&USART1->DR`，地址固定不变），还是内存数组（地址自增）。
2. **搬到哪里（目的地址）**：与源地址方向相反。
3. **搬多少个元素（数据长度 CNDTR）**：写入 CNDTR 寄存器，DMA 每完成一次搬运自动将其减 1。
4. **每次搬多宽（数据宽度）**：Byte（8 位）、Half Word（16 位）、Word（32 位）。串口通常为 Byte，ADC 为 Half Word。
5. **搬完后如何处理（传输模式）**：
   - **Normal（正常模式）**：CNDTR 减到 0 即停止，下次传输需手动再次调用 `HAL_UART_Receive_DMA`。
   - **Circular（循环模式）**：CNDTR 减到 0 后自动重载，从头开始继续搬运，永不停歇。ADC 连续采样、串口持续接收均采用此模式。

DMA 的传输方向共有三种，如图所示。

![DMA 的三种传输方向](images/dma-transfer-directions.svg)

- **外设 → 内存**：ADC 采样、串口接收。源地址固定（外设 DR），目的地址自增。
- **内存 → 外设**：串口发送、DAC 输出。源地址自增（数组），目的地址固定（外设 DR）。
- **内存 → 内存**：F103C8T6 不支持（需 DMA2 方可实现）。

---

## 10.5 DMA 关键寄存器速览

F103 的每个 DMA 通道包含 5 个寄存器：

- **DMA_CCRx**（Channel Configuration Register）：控制寄存器，包含方向（DIR）、源/目的地址自增（MINC/PINC）、数据宽度（MSIZE/PSIZE）、传输模式（CIRC）、中断使能（TCIE/HTIE/TEIE）等位域。
- **DMA_CNDTRx**（Channel Number of Data To Transfer）：待搬运元素个数，16 位，最大 65535。
- **DMA_CPARx**（Channel Peripheral Address Register）：外设地址。
- **DMA_CMARx**（Channel Memory Address Register）：内存地址。
- **DMA_ISR / DMA_IFCR**：中断状态寄存器与中断标志清除寄存器。

CubeMX 已自动完成上述寄存器的配置，使用者只需理解其背后逻辑。以 `HAL_UART_Receive_DMA` 为例，其内部大致完成以下操作：

```c
// HAL_UART_Receive_DMA 内部大致做的事：
DMA1_Channel5->CPAR = (uint32_t)&USART1->DR;  // 源：串口 DR
DMA1_Channel5->CMAR = (uint32_t)rx_buf;        // 目的：你的数组
DMA1_Channel5->CNDTR = 32;                     // 搬 32 字节
DMA1_Channel5->CCR |= DMA_CCR_CIRC |           // 循环模式
                     DMA_CCR_MINC |            // 内存地址自增
                     DMA_CCR_TCIE;             // 搬完中断使能
DMA1_Channel5->CCR |= DMA_CCR_EN;              // 启动 DMA
```

需注意，外设侧（USART1）的接收中断也必须开启，因为空闲中断由 USART 本身产生，DMA 仅负责搬运数据。

---

## 10.6 实验一：串口 DMA 接收不定长数据

串口不定长接收是工程中最常用的 DMA 应用场景：上位机发送任意长度数据帧，STM32 每接收一帧即回显一帧。

### 10.6.1 为何不能简单调用 `HAL_UART_Receive_DMA`

直觉上，该功能似乎只需调用 `HAL_UART_Receive_DMA(&huart1, buf, 32)` 即可，但实际存在以下问题：

- 上位机发送的数据长度不固定，有时为 5 字节，有时为 20 字节。
- 若设定 `Length = 32`，上位机仅发送 5 字节时，DMA 永远凑不满 32 字节，全传输中断永不触发，接收方将收不到任何数据。
- 若设定 `Length = 5`，上位机发送 10 字节时，第 6 字节起即会溢出缓冲区。

**这就是 DMA 接收不定长数据的经典难题。** 标准解法为：**循环模式 + 空闲中断（Idle Line）**。

### 10.6.2 工作原理

串口控制器内置"空闲中断"机制：当总线连续 1 个字节时间未出现数据时，硬件判定当前帧结束，触发 IDLE 中断。在中断服务函数中读取 CNDTR，用"预设长度 − 剩余长度"即可算出本帧实际接收的字节数。

F103 的 HAL 库将该流程封装为 `HAL_UARTEx_ReceiveToIdle_DMA`。

### 10.6.3 CubeMX 配置步骤

1. USART1 使能，参数为 115200-8-N-1。
2. 在 `NVIC Settings` 中勾选 `USART1 global interrupt`。
3. 切到 `DMA Settings` 标签，点击 Add：
   - DMA Request = `USART1_RX`
   - Stream/Channel = 默认（DMA1 Channel5）
   - Direction = Peripheral To Memory
   - Mode = **Circular**
   - Data Width = Byte / Byte
   - Priority = Medium
4. 生成代码。

![CubeMX DMA 配置界面](images/cubemx-dma-config.jpg)

### 10.6.4 参考代码

```c
/* USER CODE BEGIN PV */
uint8_t rx_buf[64];
/* USER CODE END PV */

/* USER CODE BEGIN 2 */
HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buf, sizeof(rx_buf));
/* USER CODE END 2 */

/* DMA 接收事件回调（接收满或空闲时触发） */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if (huart->Instance == USART1)
  {
    printf("Received %u bytes: ", Size);
    for (uint16_t i = 0; i < Size; i++) printf("%c", rx_buf[i]);
    printf("\r\n");

    // 循环模式下不用重启，但保险起见再调一次
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, rx_buf, sizeof(rx_buf));
  }
}
```

完整工程见 `code/ch10_uart_dma.c`。

### 10.6.5 验证方法

串口助手发送：

```
hello
```

STM32 串口打印：

```
Received 6 bytes: hello
```

再发送一串：

```
abcdefg
```

打印：

```
Received 8 bytes: abcdefg
```

无论发送长度如何（只要不超过 64 字节），STM32 均能正确识别帧结束并触发回调。这正是 GPS 模块、蓝牙透传、上位机通信协议普遍采用"DMA + 空闲中断"组合的原因。

### 10.6.6 典型故障点

**故障 1：循环模式下缓冲区数据被覆盖。**
循环模式中 DMA 在后台持续写入 `rx_buf`，若回调函数处理数据耗时过长，新数据会覆盖尚未处理的旧数据。解决方法：回调中第一时间将数据 memcpy 至另一缓冲区，由主循环慢慢处理；或采用双缓冲机制。

**故障 2：忘记开启 USART1 全局中断。**
DMA 中断属于 DMA1_CH5，但空闲中断属于 USART1 本身。两个 NVIC 项均需勾选。若仅勾选 DMA 中断而漏掉 USART1 中断，接收回调将永不触发。

**故障 3：回调中重复调用 `HAL_UART_Receive_DMA`。**
循环模式下理论上无需重启，DMA 自行运行。但部分 HAL 版本在首次回调后 CNDTR 会停止计数，因此保险起见可在回调末尾再次调用一次。需注意：再次调用时**不应**在回调中执行耗时操作，否则会丢失下一帧数据。

**故障 4：回调中 printf 阻塞。**
回调内的 `printf` 是阻塞调用（底层走 `HAL_UART_Transmit`），若打印字符较多，回调将长时间占用 CPU。短数据场景影响不大；长数据场景建议改为：回调中仅置标志位并 memcpy，由主循环完成 printf。

---

## 10.7 实验二：ADC + DMA 连续采样（回顾）

第 9 章 9.6 节已给出如下调用：

```c
HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buf, 2);
```

学习本章后，应能理解其背后的完整逻辑：

- ADC1 配置为扫描 + 连续模式，每转换完一个通道即发出 DMA 请求。
- DMA1_CH1 配置为循环模式，源地址固定为 `&ADC1->DR`，目的地址自增指向 `adc_buf`。
- 两个通道轮流转换，DMA 自动将结果填入 `adc_buf[0]`、`adc_buf[1]`。
- CPU 完全不参与搬运过程，主循环可随时读取最新值。

![ADC + DMA 连续采样的数据流](images/adc-dma-continuous-block.svg)

若希望"采满 N 个点通知一次"，可同时开启 DMA 的半传输中断（HT）与全传输中断（TC）：

```c
// 在 CubeMX DMA Settings 里把 "Half Transfer Interrupt" 勾上
// 或者在代码里：
SET_BIT(DMA1_Channel1->CCR, DMA_CCR_HTIE);

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc) {
  // adc_buf 前一半已经填满，可以处理了
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
  // adc_buf 后一半已经填满
}
```

上述机制即"双缓冲"：DMA 写后半段时 CPU 读前半段，DMA 写前半段时 CPU 读后半段，二者互不冲突。示波器、声音采集等高速数据采集场景均依赖该技巧。

---

## 10.8 实验三：串口 DMA 发送

接收流程介绍完毕后，发送流程更为简单：

```c
/* USER CODE BEGIN 2 */
char msg[] = "Hello via DMA TX\r\n";
HAL_UART_Transmit_DMA(&huart1, (uint8_t*)msg, sizeof(msg)-1);
/* USER CODE END 2 */
```

DMA 自动将 `msg` 中的字节逐个送入 USART1_DR，CPU 完全不参与。发送完成后触发 `HAL_UART_TxCpltCallback`。

工程中常将阻塞式日志打印改写为 DMA 发送。常规写法：

```c
printf("...");   // 这个是阻塞的
```

改写为：

```c
char log_buf[128];
int n = snprintf(log_buf, sizeof(log_buf), "...");
HAL_UART_Transmit_DMA(&huart1, (uint8_t*)log_buf, n);
```

改写后主循环可立即继续运行，串口在后台逐步发送。需强调 `log_buf` 必须定义为全局变量或 static 变量，不能使用栈上局部变量——DMA 是异步的，函数返回后栈帧即被回收。

---

## 10.9 DMA 与中断优先级配置

一种典型故障现象为：ADC 以 DMA 连续采样（1 kHz）的同时，串口 DMA 接收频繁丢字节。其排查过程如下：

- ADC 全传输中断默认优先级为 5，串口中断优先级同样为 5。
- ADC 中断服务函数执行需数十微秒，串口字节到达时被 ADC 中断阻塞。
- 串口 DR 仅有单字节 FIFO，被阻塞超过两个字节即发生溢出。

**解决原则：将高速后台外设（ADC、SPI 接收）的中断优先级调低，将实时性要求高的外设（串口接收、外部中断）优先级调高。**

在 CubeMX 的 `NVIC` 标签中修改优先级数值，数字越小优先级越高。一套参考配置为：

- SysTick：15（最低）
- 串口接收：5
- ADC 转换完成：7
- 外部按键中断：3

---

## 10.10 常见故障与调试清单

本节集中列出 DMA 使用中的高频故障，便于出现问题时逐项对照排查。

**1. DMA 无反应，缓冲区始终为 0。**
- 检查外设是否真正使能 DMA：ADC 需在 CR2 中开启 DMA 位（`HAL_ADC_Start_DMA` 内部完成），USART 需在 CR3 中开启 DMAT/DMAR 位（`HAL_UART_Receive_DMA` 内部完成）。若采用寄存器方式编程，该位容易遗漏。
- 检查 DMA 通道使能位（EN）是否已置位。

**2. 仅触发一次回调即停止。**
- 模式被误设为 Normal 而非 Circular。Normal 模式下 CNDTR 减到 0 即停止，需手动重启。

**3. 缓冲区中数据错位。**
- 数据宽度不匹配。外设为 16 位（ADC_DR）时，若 DMA 配为 Byte，会将一个 16 位数拆为两个字节搬运，数组布局完全错乱。CubeMX 中 Peripheral 与 Memory 的 Data Width 必须保持一致。

**4. 内存地址自增未开启。**
- CPAR（外设地址）与 CMAR（内存地址）的自增控制是相互独立的。外设 DR 地址固定，不应自增；内存数组必须自增。若两者设置颠倒，DMA 会将整个数组写入同一地址。

**5. 回调函数未进入。**
- 检查 NVIC：DMA 中断与外设中断均需开启。
- 检查中断优先级分组（NVIC_PriorityGroup）。CubeMX 默认配置为 4 位抢占优先级 + 0 位子优先级，若手动修改过分组，可能导致中断不触发。

**6. DMA 发送时数据内容错乱。**
- `tx_buf` 为栈上局部变量，DMA 异步发送期间函数已返回，栈帧被回收。应改为全局变量或 static 变量。

**7. 接入 DMA 后 printf 仍阻塞。**
- `printf` 默认走 `HAL_UART_Transmit`（阻塞版）。若希望以 DMA 方式发送日志，需将 `_write` 函数重定向至 `HAL_UART_Transmit_DMA`，但此时 printf 变为异步，多任务环境下需加锁。简单项目不建议做此改动。

---

## 10.11 DMA 的适用场景判断

一个常见的设计问题是："是否所有数据传输都应使用 DMA？" 答案是：**视数据量与频率而定。**

- **偶尔发送少量字节**（例如每秒一次 printf）：使用阻塞方式即可，DMA 配置开销大于收益。
- **高频连续数据流**（ADC 1 kHz 采样、串口持续接收 GPS、SPI 驱动 OLED 刷屏）：必须使用 DMA，否则 CPU 无法承受。
- **大块内存拷贝**（例如整帧图像缓存）：DMA 为首选。虽然 F103C8T6 不支持内存到内存 DMA，但可借助 SPI 将内存数据搬至外部 Flash。
- **偶尔的短命令**（例如接收 1 字节控制 LED 开关）：使用中断即可，DMA 属于杀鸡用牛刀。

工程上的经验法则为：**单次搬运少于 16 字节，或频率低于 100 Hz 时，不必使用 DMA；超过该量级，DMA 几乎总是更优选择。**

---

## 10.12 DMA 一次完整搬运的生命周期

为完整呈现 DMA 的运行过程，本节以 USART1_RX 接收 32 字节为例，按时间轴拆解整个流程。

**阶段 0：CPU 配置**
1. 应用层调用 `HAL_UARTEx_ReceiveToIdle_DMA`。
2. HAL 将 `&USART1->DR` 写入 `DMA1_CPAR5`。
3. 将 `rx_buf` 数组地址写入 `DMA1_CMAR5`。
4. 将 32 写入 `DMA1_CNDTR5`。
5. 配置 `DMA1_CCR5`：外设地址不增、内存地址自增、Byte/Byte 宽度、Circular 模式、开启传输完成与半传输中断。
6. 置位 `DMA1_CCR5` 的 EN 位，DMA 开始工作。
7. 置位 USART1_CR3 的 DMAR 位，允许串口向 DMA 发送请求。

**阶段 1：第一个字节到达**
8. USART1 接收一个字节，硬件将其置入 DR，置位 RXNE 标志。
9. 由于 DMAR 已开启，USART1 同时向 DMA1_CH5 发出硬件请求。
10. DMA 仲裁器批准该请求（此时 CPU 可能在执行其他任务，让出总线）。
11. DMA 读取 `&USART1->DR`（取得该字节），写入 `CMAR5` 指向的地址。
12. CNDTR5 由 32 减为 31，CMAR5 自动加 1（指向下一个数组元素）。
13. 该字节搬运完成，CPU 全程未参与。

**阶段 2：连续接收 32 个字节**
14. 重复第 8~13 步，每个字节均由 DMA 独立完成搬运。
15. 中途 CNDTR5 减至 16 时，DMA 触发半传输中断（HT），CPU 可进入处理前 16 个字节。

**阶段 3：收满或总线空闲**
16. 若 CNDTR5 减至 0，DMA 触发全传输中断（TC）；Circular 模式下 CNDTR 自动重载为 32，从头开始新一轮搬运。
17. 若中途串口总线空闲 1 字节时间，USART1 自行触发 IDLE 中断，CPU 进入中断读取 CNDTR5，计算实际接收字节数，并回调 `HAL_UARTEx_RxEventCallback`。

整个过程中 CPU 仅在第 17 步（以及半传输中断）被打断，中间字节搬运全部由硬件完成。这正是 DMA 在 115200 波特率下能将 CPU 占用率降至 0.1% 以下的原因。

---

## 10.13 DMA 调试三步法

DMA 不工作时，不应盲目修改代码，建议按以下顺序排查。

**第一步：观察 CNDTR 是否递减。**
在 Keil / STM32CubeIDE 调试模式下，将 `DMA1_Channel5->CNDTR` 加入 Watch 窗口。若外设正在发送数据，CNDTR 应持续减小。若 CNDTR 不变，说明外设根本未发出 DMA 请求——应回查 USART_CR3 的 DMAR 位或 ADC_CR2 的 DMA 位是否正确置位。

**第二步：查看 ISR 中断标志。**
`DMA1->ISR` 为只读寄存器，每一位对应一个通道的事件（TCIF、HTIF、TEIF）。若已开启中断但回调未进入，应检查 ISR 中对应位是否已置位但未被清除。HAL 内部会自动清标志，但若采用寄存器方式编程，需自行写 `DMA1->IFCR` 清除。

**第三步：在回调中翻转 LED。**
最简单有效的手段是：进入回调即翻转 LED。若 LED 不闪，说明回调根本未进入，问题出在 DMA 配置或 NVIC；若 LED 闪烁但数据异常，问题出在数据宽度或地址自增配置。

工程上的一条通用经验为：**任何 DMA 工程，第一步先接 LED 验证回调是否触发，第二步再检查数据内容。** 不要一上来就使用 printf，因为 printf 本身是阻塞调用，会掩盖真实的时序问题。

---

## 10.14 DMA 发送的完成回调与异步陷阱

发送流程虽比接收简单，但存在一个典型的异步陷阱：

```c
char msg[] = "hello";
HAL_UART_Transmit_DMA(&huart1, (uint8_t*)msg, 5);
// 下一行立刻改 msg
strcpy(msg, "world");   // ← 危险！DMA 可能还在发 "hello"
```

DMA 是异步的：调用 `HAL_UART_Transmit_DMA` 后函数立即返回，但 DMA 仍在后台逐字节向 DR 搬运。若此时立即修改 `msg`，DMA 可能在搬运中途读到新值，串口上输出的将是 `hworle` 之类的乱码。

正确做法如下：

```c
// 方法 1：用发送完成回调
volatile uint8_t tx_done = 1;
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) tx_done = 1;
}

// 发送前等上一次发完
while (!tx_done);
tx_done = 0;
HAL_UART_Transmit_DMA(&huart1, (uint8_t*)msg, 5);

// 方法 2：短数据直接用阻塞版，DMA 留给长 buffer
```

短数据使用阻塞版 `HAL_UART_Transmit` 完全可行，代码简洁且无异步陷阱。DMA 的真正用武之地是长缓冲区与连续数据流场景。

---

## 10.15 双缓冲：ADC 采样与处理并行

本节介绍一种进阶技巧，适用于示波器、声音采集、高速数据记录等场景。

基本思路为：DMA 配置为 Circular 模式，缓冲区长度为 N。DMA 搬运前 N/2 个元素时触发半传输中断（HT），搬运后 N/2 个元素时触发全传输中断（TC）。CPU 在 HT 中处理前半段数据，在 TC 中处理后半段数据——**DMA 与 CPU 各使用缓冲区的一半，互不冲突**。

```c
/* USER CODE BEGIN 2 */
HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buf, 128);  // 128 个点
/* USER CODE END 2 */

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc) {
  // 前 64 个点已经采完，DMA 在后台采后 64 个
  process_chunk(&adc_buf[0], 64);
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
  // 后 64 个点采完，DMA 又回去采前 64 个
  process_chunk(&adc_buf[64], 64);
}
```

该模式的优势在于：无需停止 ADC 即可处理数据，吞吐率比"采满 N 个点后停止、处理完再启动"高出两倍以上。FFT 频谱分析、电流波形记录、振动传感器采集等应用均依赖此机制。

---

## 10.16 本章小结

- DMA 是独立于 CPU 的硬件搬运单元，CPU 完成配置后即可空闲，仅在搬运结束时被通知。
- F103C8T6 仅包含 DMA1，共 7 个通道；通道与外设硬件固定映射，ADC1 → CH1、USART1_RX → CH5、USART1_TX → CH4。
- DMA 五大传输要素：源地址、目的地址、数据长度、数据宽度、传输模式（Normal/Circular）。
- 串口不定长接收应使用 `HAL_UARTEx_ReceiveToIdle_DMA`（循环模式 + 空闲中断）。
- ADC 连续采样使用 `HAL_ADC_Start_DMA`，配合扫描 + 循环模式，CPU 完全不参与。
- 中断优先级需合理分配：实时性要求高的（串口接收）优先级调高，后台批量传输（ADC 采样）优先级调低。
- 并非所有场景都应使用 DMA：小数据量、低频率场景使用阻塞或中断方式更简洁。

---

## 练习题

1. 与 CPU 逐字节搬运相比，DMA 可将串口接收的 CPU 占用率降至多少？其根本原因是什么？
2. 在 F103C8T6 上，ADC1 与 USART1_RX 分别挂在 DMA1 的哪个通道？二者能否同时使用 DMA？USART1_TX 与 TIM4_CH4 共用 CH4 时会出现什么现象？
3. Normal 模式与 Circular 模式有何区别？ADC 连续采样与单次串口命令接收分别应选用哪种模式？
4. 串口接收不定长数据时，为何必须采用"DMA + 空闲中断"组合？若仅使用 `HAL_UART_Receive_DMA` 并固定 Length，会出现什么问题？
5. ADC_DR 为 16 位寄存器，DMA 的 Data Width 为何不能配为 Byte？若误配会导致什么后果？
6. 在 DMA 接收回调中直接 printf 大量数据可能引发什么问题？应如何规避？
7. 双缓冲模式下，半传输中断（HT）与全传输中断（TC）分别对应缓冲区的哪一半？
8. 为何 `tx_buf` 必须定义为全局或 static 变量，而不能是函数内的栈局部变量？

---

## 综合实践建议

建议将本章与第 9 章内容串联，完成一个"ADC 采样 + DMA 搬运 + 串口 DMA 上传"综合项目：PA0 接电位器，ADC 以 1 kHz 采样，DMA 在后台将数据搬入数组，串口再通过 DMA 将每 16 个点发送至上位机绘制曲线。该链路是嵌入式系统中最经典的信号采集通路，跑通后即可视为对 ADC 与 DMA 形成了完整理解。

---

<!-- ===== 第10章 完 ===== -->

上一章：[第9章 ADC 模拟量采集](ch09_ADC.md)　下一章：[第11章 I2C / SPI 通信（OLED 实战）](ch11_I2C_SPI与OLED.md)
