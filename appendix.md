# 附录：排查表、参考资料、学习路线

> 本附录为**查询型文档**，供工程实践中快速检索，而非顺序阅读。当项目无法烧录、OLED 无显示、FreeRTOS 进入 HardFault 等异常出现时，查阅对应条目。所有结论均来自正文各章的工程实践记录与官方文档。

---

## 附录 A 常见问题排查总表

本附录统一采用"问题—现象—原因—处理"体例组织。各表"症状"列对应工程中可观察到的现象，"可能原因"列为根因分析，"解决"列为处理步骤。

### A.1 烧录与连接

| 问题（现象） | 可能原因 | 处理方法 |
|---|---|---|
| ST-Link 无法识别 | 驱动未安装 / USB 线为充电线 / 接线错误 | 安装 ST-Link 驱动；更换数据线；核对 SWDIO→PA13、SWCLK→PA14、GND→GND、3.3V→3.3V |
| 烧录提示 "No target connected" | SWD 引脚被复用为其他功能（例如 PB6 被 I2C 占用；SWD 位于 PA13/PA14，一般不冲突） | 按住复位再点烧录；或 BOOT0 接 3.3V 使芯片进入系统存储器模式，使用 STM32CubeProgrammer 全片擦除 |
| 芯片被锁死（读保护 RDP） | 此前开启了读保护 | STM32CubeProgrammer 的 Option Bytes 中将 RDP 改为 0xAA（会触发全片擦除） |
| 烧录完成但不运行 | 未开启 Reset and Run / 晶振配置错误 / 中断向量表未对齐 | Keil Debug 配置勾选 "Reset and Run"；确认 HSE 为 8 MHz（Blue Pill 板载晶振）；检查 startup 文件 |
| PlatformIO 克隆 ST-Link 报 CPUTAPID 错 | 廉价 ST-Link 固件 ID 不匹配 | 在 `upload_flags` 中加入 `-c set CPUTAPID 0x2ba01477` |

![ST-Link SWD 四线接线图](images/swd-wiring.svg)

### A.2 GPIO 与 LED

| 问题（现象） | 可能原因 | 处理方法 |
|---|---|---|
| LED 不亮 | 引脚错误 / 电平相反 / 正负极接反 / 未串联电阻 | 对照[第 3 章 GPIO](ch03_GPIO点LED.md) 接线表；Blue Pill 板载 LED 位于 PC13，低电平点亮；外接 LED 串联 220Ω~1kΩ 电阻 |
| LED 微亮 | 引脚配置为模拟输入 / 引脚悬空 | 配置为 GPIO_Output_PushPull |
| 按键按下无反应 | 未加上拉 / 下拉 / 未做消抖 / EXTI 未配置 | 内部上拉 + 外部 0.1 μF 电容；使用[第 5 章中断](ch05_EXTI中断.md)的消抖状态机 |

### A.3 时钟与启动

| 问题（现象） | 可能原因 | 处理方法 |
|---|---|---|
| 串口波特率不对（乱码） | 时钟树配置错误，HSE 非 8 MHz | CubeMX RCC 中 HSE 选择 Crystal/Ceramic Resonator；Clock Configuration 中确认 SYSCLK = 72 MHz |
| 延时不对（`HAL_Delay` 慢一倍） | SysTick 时钟源配置错误 | CubeMX 中 SysTick 保持默认，勿手动修改 |
| 运行中进入 HardFault | 栈溢出 / 空指针 / 数组越界 | 使用调试器查看 PC 指针定位；常见原因为数组越界或空指针 |

### A.4 串口

| 问题（现象） | 可能原因 | 处理方法 |
|---|---|---|
| 串口全为乱码 | 两端波特率不一致 / TX/RX 未交叉 / 未共地 | 两端波特率严格一致（常见 115200）；MCU_TX→USB-RX、MCU_RX→USB-TX；共接 GND |
| printf 无输出 | Keil 未开启 MicroLIB / fputc 未重定向 | Options → Target 勾选 "Use MicroLIB"；将 fputc 重定向至 USART1_DR（参见[第 8 章](ch08_串口.md)） |
| 浮点 printf 显示错误 | CubeIDE 默认未链接浮点 printf | 链接选项加入 `-u _printf_float` |
| 串口丢字节 | 阻塞接收跟不上波特率 | 使用[第 10 章 DMA + 空闲中断](ch10_DMA.md)方案 |

### A.5 定时器与 PWM

| 问题（现象） | 可能原因 | 处理方法 |
|---|---|---|
| 定时中断不进入 | 未启动定时器 / NVIC 未勾选 | 调用 `HAL_TIM_Base_Start_IT(&htimx)`；CubeMX NVIC 中勾选 TIMx global interrupt |
| PWM 无输出 | 通道未启动 / 占空比寄存器未写 | 调用 `HAL_TIM_PWM_Start(&htimx, TIM_CHANNEL_x)`；修改 `__HAL_TIM_SET_COMPARE` |
| PWM 频率不对 | PSC / ARR 计算错误 | 频率 = 72 MHz / ((PSC+1) × (ARR+1))，参见[第 7 章 PWM](ch07_PWM.md) |
| 舵机抖动 | 电源功率不足 / 地线未共接 | 舵机独立供电，信号地与 MCU 地必须连通 |

### A.6 ADC

| 问题（现象） | 可能原因 | 处理方法 |
|---|---|---|
| ADC 读数跳变 | 未滤波 / 输入阻抗过高 | 输入端加 0.1 μF 电容至 GND；多次采样取平均；参见[第 9 章 ADC](ch09_ADC.md) |
| ADC 满量程不对 | 参考电压非 3.3 V | F103 默认 VREF+ 接 3.3 V；可使用内部 VREFINT（1.2 V）进行校准 |
| DMA 采样中途停止 | DMA 循环模式未开启 / 缓冲区未对齐 | CubeMX 中 DMA 模式选择 Circular；buffer 使用 `__attribute__((aligned(4)))` |

### A.7 I2C / SPI / OLED

| 问题（现象） | 可能原因 | 处理方法 |
|---|---|---|
| I2C 完全无响应 | 接线反 / 地址错 / 无上拉 | 运行 I2C Scanner（参见[第 11 章](ch11_I2C_SPI与OLED.md) 11.4.3）；确认 PB6 = SCL、PB7 = SDA；模块板通常已带上拉 |
| OLED 无显示 | 电荷泵命令未发送 / 地址错 / 遗漏 UpdateScreen | 更换完整初始化序列；确认地址为 0x78 还是 0x7A；每次绘制后必须调用 `ssd1306_UpdateScreen()` |
| OLED 花屏 | 显存缓冲未清零 / 初始化顺序错 | 启动时调用 `ssd1306_Fill(Black)`；按数据手册顺序发送初始化命令 |
| I2C 卡死（HAL 返回 BUSY） | 从机将 SDA 拉死 | 软件层面发送 9 个 SCL 时钟释放总线；或重启 MCU |
| SPI 接屏幕乱码 | CPOL/CPHA 不匹配 | 在 Mode 0 与 Mode 3 之间切换；参见[第 11 章](ch11_I2C_SPI与OLED.md) 11.3.2 |
| 多个 I2C 设备仅一个可用 | 地址冲突 | 运行 Scanner；修改 ADDR 跳线；或加装 TCA9548A 多路复用芯片 |

### A.8 FreeRTOS

| 问题（现象） | 可能原因 | 处理方法 |
|---|---|---|
| 启动即 HardFault | 栈过小 / 堆不足 | 任务栈从 128 调整至 256~512；加大 `configTOTAL_HEAP_SIZE` |
| 任务表现为轮流卡住 | 任务中使用了 `HAL_Delay` | 替换为 `vTaskDelay(pdMS_TO_TICKS(...))` |
| 按键中断一按即异常 | 中断中调用了普通 API / 中断优先级过高 | 使用 `...FromISR` 版本；中断优先级设为 ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY`（默认 5） |
| 队列收不到消息 | 句柄拼错 / 队列未创建 | 对照 `freertos.c` 中的实际句柄名修改 |
| 随机 HardFault | 栈溢出踩内存 | 使用 `uxTaskGetStackHighWaterMark` 测量每个任务剩余栈空间 |
| RAM 不足 | 任务栈 + 队列 + 内核超过 20 KB | 缩减栈大小、缩减队列长度、用任务通知代替队列 |

---

## 附录 B 参考资料索引

> 本讲义的所有结论均可追溯至下列来源。标注"已核验"者，表示撰写时已对照原文确认关键事实。

### B.1 ST 官方文档（一手）

- **STM32F103x8/xB 数据手册（DS5319）**：芯片规格、引脚定义、电气特性。
  已核验：72 MHz / 64 KB Flash / 20 KB RAM / 48 引脚 / 2.0–3.6 V。
  https://www.st.com/resource/en/datasheet/stm32f103c8.pdf

- **STM32F10xxx 参考手册（RM0008）**：GPIO、时钟树、NVIC、TIM、ADC、USART、DMA、I2C、SPI 全部外设的寄存器定义。该手册篇幅最大、参考价值最高，遇到寄存器级问题时应优先查阅。
  已核验：时钟树、GPIO BSRR/BRR、APB1/APB2 分频、定时器 2 倍频规则、I2C 寄存器 CR1/CR2/SR。

- **STM32CubeF1 固件包**：HAL 库源码 + 大量例程。ST 官网免费下载，例程质量优于多数第三方教程。

- **AN2825（STM32F10xxx I2C optimized examples）**：ST 官方发布的 I2C 应用指南，详述 F103 硬件 I2C 的硅片缺陷与规避方法。

- **DM00105262（Developing applications on STM32Cube with RTOS）**：FreeRTOS + STM32 的官方应用笔记，中断优先级规则的权威来源。

### B.2 中文教程体系

![正点原子《精通 STM32F4 HAL 库版》](images/book-zdzy-stm32.jpg)

- **正点原子《STM32 库开发指南》**：国内最系统的中文教程体系，配套文档、视频与开发板。本讲义的章节组织思路参考了该体系。需注意正点原子的开发板基于 F103/F407，引脚映射与 Blue Pill 略有差异，移植时应核对。

![野火《STM32 库开发实战指南：基于 STM32F103》](images/book-yehuo-stm32f103.jpg)

- **野火《STM32 库开发实战指南：基于 STM32F103》**：库开发方式的另一套体系，对 F103 讲解细致，寄存器层面比正点原子更深入。可在孔夫子旧书网、京东等渠道获取。

- **江协科技 / 韦东山**：视频教程，入门友好。阅读文档遇到困难时，可通过视频建立直观认识。

- **FreeRTOS 官方文档**：https://www.freertos.org 。任务、队列、信号量、中断规则、Cortex-M3 移植等内容，均为一手资料。

- **WaveShare / syuez SSD1306 示例代码**：GitHub 搜索 "stm32-ssd1306"，最常用的轻量 OLED 驱动。已核验：地址 0x78、SPI Mode、初始化序列。

### B.3 社区与博客

- **CSDN / 博客园**：检索具体问题（如"STM32 I2C 卡死"、"FreeRTOS HardFault"），工程实践记录丰富。内容质量参差，但高赞条目通常可解决问题。
- **PlatformIO Community**：Blue Pill + 克隆 ST-Link 的 CPUTAPID 问题的主要讨论来源。
- **STM32 中文论坛**（bbs.21ic / EDAA）：历史帖子丰富，多数工程问题均有人遇到过。

### B.4 论文（可选延伸）

完成毕业设计或撰写原理综述时可检索以下方向：

- 嵌入式系统教学类：ARM Cortex-M 内核与 HAL 库在实验教学中的应用。
- RTOS 调度类：FreeRTOS 实时调度与优先级反转处理。
- PWM / ADC 工程应用类：电机控制、电源管理方向的论文。

上述文献并非本讲义的直接依据，用于拓展深度，按需检索。

---

## 附录 C 学习路线图

### C.1 本讲义的学习路径

本讲义十二章按以下顺序组织，初学者应按顺序学习，不宜跳跃：

```
阶段一：认知与工具（第 1~3 章）
  ├─ ch01 硬件认知：Blue Pill、ST-Link、STM32 是什么
  ├─ ch02 环境搭建：CubeMX + Keil / CubeIDE
  └─ ch03 GPIO 点 LED：第一个程序、烧录、调试

阶段二：核心外设（第 4~9 章）
  ├─ ch04 时钟与启动：HSE、PLL、72MHz、启动流程
  ├─ ch05 EXTI 中断：按键、NVIC、消抖
  ├─ ch06 定时器：基本定时、中断、编码器
  ├─ ch07 PWM：呼吸灯、舵机
  ├─ ch08 串口：printf、收发、环形缓冲
  └─ ch09 ADC：采样、电位器、NTC

阶段三：工程化（第 10~11 章）
  ├─ ch10 DMA：串口不定长接收、ADC 连续采样
  └─ ch11 I2C / SPI：OLED、AHT20、BMP280

阶段四：操作系统（第 12 章）
  └─ ch12 FreeRTOS：多任务、队列、信号量、中断配合
```

### C.2 综合项目清单

完成下列项目即视为掌握了前序章节。每个项目均串联了若干章的知识点：

1. **呼吸灯 + 按键调速**（ch03 + ch05 + ch07）
2. **串口控制 LED**（ch05 + ch08）：上位机发送 1/0，板上 LED 亮灭
3. **ADC 电位器调 PWM**（ch07 + ch09）：调节电位器，LED 呼吸频率随之变化
4. **OLED 显示串口数据**（ch08 + ch11）：串口收到什么，OLED 即显示什么
5. **环境监测仪**（ch09 + ch11）：AHT20 + BMP280 + OLED，实时显示温湿度与气压
6. **FreeRTOS 环境监测仪**（ch11 + ch12）：传感器任务、显示任务、按键任务独立拆分
7. **智能小车 / 蓝牙透传**（ch07 + ch08 + ch11）：HC-05 蓝牙模块，手机 App 控车

### C.3 后续学习方向

完成本讲义后，可沿以下方向继续深入：

- **进阶外设**：输入捕获（频率测量）、看门狗（IWDG/WWDG）、低功耗（STOP/Standby 模式）、USB（虚拟串口）、CAN（工业车规）、以太网（LWIP）。
- **更换芯片**：STM32F4（高性能，带 FPU 与 DMA2）、STM32H7（Cortex-M7，主频 400 MHz+）、STM32G0（低成本新系列）。
- **深入 RTOS**：FreeRTOS 熟练后可研究 RT-Thread（组件化、中文文档完善）、ThreadX（微软系）、Zephyr（Linux 基金会）。
- **产品化方向**：PCB 设计（立创EDA、KiCad）、EMC、低功耗设计、BOOTLOADER（IAP 在线升级）。
- **上层应用**：Linux 嵌入式（树莓派、全志、IMX6ULL）、QT 嵌入式界面、物联网（MQTT、云平台）。

### C.4 学习建议

以下为经过工程验证的学习原则：

- 初学者不宜直接啃 RM0008 参考手册，应先把外设用起来，遇到问题再回查。
- 不应迷信"必须寄存器开发"的教条，HAL 库足以支持 90% 的项目；待有性能要求时再回归寄存器层面。
- 开发板不必购置过多，一块 Blue Pill（约 15 元）配合若干模块（总计约 100 元），即可完成 FreeRTOS 阶段的全部练习。
- 遇到问题先检索，检索未果再提问。绝大多数问题前人已遇到过。
- 代码应纳入版本管理（git），便于修改出错时回滚。
- 焊接条件有限时，应使用面包板配合母对母跳线，避免在飞线上耗费过多时间。
- 示波器并非必需，但逻辑分析仪（约 20 元的入门型号）强烈推荐——对 I2C/SPI/串口调试极有价值，效率远高于依赖 printf 猜测。

### C.5 最小硬件清单

若手头尚无任何器材，按以下清单采购即可完成本讲义全部示例：

| 物品 | 数量 | 参考价格 |
|---|---|---|
| STM32F103C8T6 Blue Pill 核心板 | 1 | ¥15 |
| ST-Link V2 下载器（克隆版） | 1 | ¥15 |
| USB-TTL CH340 串口模块 | 1 | ¥8 |
| 0.96 寸 SSD1306 OLED (I2C) | 1 | ¥8 |
| AHT20 温湿度模块 | 1 | ¥6 |
| BMP280 气压模块 | 1 | ¥6 |
| 面包板 + 杜邦线包 | 1 | ¥15 |
| 按键、LED、电位器、电阻杂包 | 1 | ¥10 |

全套合计约 80~100 元，即可跑通本讲义所有示例。

---

<!-- ===== 附录完 ===== -->
