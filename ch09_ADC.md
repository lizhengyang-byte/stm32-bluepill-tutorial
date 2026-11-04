# 第9章 ADC 模拟量采集：从电位器到光敏电阻

> 这一章我要带你把 STM32 从"只会输出高低电平"拉回到真实世界。前几章你点过灯、扫过按键、做过 PWM、收过串口，那些信号要么是 0 要么是 1，要么是一段固定频率的方波。可现实里的传感器——旋钮、光敏电阻、热敏电阻、音频——吐出来的都是连续变化的电压，这就得靠 ADC 来采样。

我第一次做 ADC 实验的时候，天真地以为读出来的数字值除以 4095 乘 3.3 就是电压，拿万用表一对，结果差了 0.2V，还漂。折腾了一下午才明白：参考电压不是理想的 3.3V，采样时间没设够，前端电阻太大把采样电容拖慢了。这一章我把这些坑都摊开讲。

---

## 9.1 为什么 ADC 是单片机和现实世界之间的桥

数字世界只有 0 和 1。现实世界是连续的：你旋钮拧到哪一格、房间亮度暗了多少度、手摸上去温度升高了几摄氏度，这些都是模拟量。单片机要感知它们，必须先把电压"切"成有限个台阶——这就是 ADC（Analog-to-Digital Converter，模数转换器）干的活。

F103C8T6 内部自带的 ADC 是这样一个东西：

- **12 位逐次逼近型（SAR）**：每次转换输出一个 0~4095 的整数，因为 2^12 = 4096 个台阶。
- **最多 18 个外部通道**：F103C8T6 实际引出到引脚的是 ADC1 的 IN0~IN9，对应 PA0~PA7、PB0、PB1。
- **参考电压 V_REF+**：Blue Pill 板子上直接接到 3.3V，没有单独引出 VREF+ 引脚（LQFP48 封装把 VREF+ 内部连到 VDDA）。
- **最高 1 MSPS**：ADC 时钟最高 14 MHz，转换一次最少 14 个周期，算下来每秒约 100 万次采样。

转换公式就一行：

```
数字值 N = V_in / V_ref × 4095
V_in    = N × V_ref / 4095
```

以 V_ref = 3.3V 为例，1 LSB（最低有效位）代表的电压是 3.3/4095 ≈ 0.806 mV。也就是说，你 ADC 能分辨的最小电压变化大约是 0.8 mV。

![ADC 电位器分压原理：数字值=电压/3.3×4095](images/adc-potentiometer.svg)

举个例子：电位器中间滑动端输出 1.65V，那么 ADC 读出来是 1.65/3.3×4095 ≈ 2047，正好是量程的一半。你把旋钮拧到两端，就应该读到 0 或者 4095。

### 9.1.1 别被"12 位"骗了：量化误差和有效位数

很多教程写到这里就停了，但我得提醒你一句：**12 位是理论分辨率，不是精度。**

ADC 把连续电压切成 4096 个台阶，本身就有 ±0.5 LSB 的量化误差。再加上参考电压的误差、内部电阻网络的温漂、电源噪声，你实际拿到的"有效位数"（ENOB）往往只有 10~11 位。这意味着：

- 你用 ADC 去做电压表，不要指望它比万用表准；
- 你用 ADC 读电位器旋钮，1000 以内的数值波动基本是噪声；
- 做温度测量时，**软件上多次采样取平均**比追求位数更有效。

我自己的习惯是：采集到的原始值先右移两位（除以 4），相当于把 12 位当 10 位用，曲线立刻就平滑了。

### 9.1.2 SAR 型 ADC 到底在干什么

"逐次逼近"这四个字听着玄，其实逻辑特别朴素。你可以把它想象成一个人在天平上从大到小试砝码：

1. **采样阶段**：ADC 内部一个小采样电容通过开关接到你的引脚，给它充电到 V_in。这个阶段需要的时间由 SMPR 寄存器配置。
2. **保持阶段**：开关断开，电容上的电压保持住，和外界断开。
3. **逐次比较**：SAR 逻辑从最高位开始，先猜"是不是 2048？"，内部 DAC 输出 2048 对应的电压和电容上的 V_in 比一下；如果 V_in 大，就把这一位保留，否则清零；然后猜 1024、512……一直到最低位。整个过程 12 次比较，固定 12.5 个 ADC 时钟周期。
4. **结果写入 DR**：全部比完，12 位结果塞进 ADC_DR 寄存器，硬件置 EOC 标志。

![SAR 型 ADC 一次转换的时序](images/adc-sar-timing.svg)

这里有个经常被忽略的参数：**采样时间**。CubeMX 里 SMPR 默认给的是 1.5 个 ADC 周期，听着很快，但前提是你的信号源内阻很小。如果你前面挂的是光敏电阻 + 10KΩ 分压，等效源阻抗可能到几千欧，1.5 个周期根本不够把采样电容充满，读出来的值会偏小还跳。我一般直接把采样时间调到 55.5 或 71.5 周期，多花几个微秒，换来数据稳定。

---

## 9.2 引脚和通道：PA0 到底是哪一路

F103C8T6 这个 LQFP48 封装把 ADC1 的通道引到了 10 个引脚：

| 通道 | 引脚 |
|---|---|
| ADC1_IN0 | PA0 |
| ADC1_IN1 | PA1 |
| ADC1_IN2 | PA2 |
| ADC1_IN3 | PA3 |
| ADC1_IN4 | PA4 |
| ADC1_IN5 | PA5 |
| ADC1_IN6 | PA6 |
| ADC1_IN7 | PA7 |
| ADC1_IN8 | PB0 |
| ADC1_IN9 | PB1 |

内部还有两个特殊通道：ADC1_IN16 是内部温度传感器（测芯片自己的温度，不是测环境），ADC1_IN17 是内部参考电压 VREFINT（约 1.2V），用来反算外部 VDDA 到底准不准。

![STM32F103C8T6 Blue Pill 完整引脚图](images/stm32f103-pinout-adc.jpg)

注意一个细节：**一个引脚不能同时是 ADC 输入和别的外设**。CubeMX 里你点 PA0，选了 `ADC1_IN0` 之后，它就自动进入 Analog 模式，数字输入缓冲器断开。这时候你再想拿 PA0 当 GPIO 读按键，是读不出来的——这是新手最常踩的坑。

### 9.2.1 ADC 寄存器速览（HAL 已经帮你写了，知道就行）

你用 HAL 库其实不用手碰寄存器，但调试出问题的时候，翻一下参考手册能救命。我把关键寄存器列在下面：

![F103 ADC1 关键寄存器](images/adc-register-map.svg)

简单说：

- `ADC_CR2`：ADON 位开/关 ADC，SWSTART 位软件触发一次转换，ALIGN 位选左对齐还是右对齐。
- `ADC_CR1`：SCAN 位开扫描模式，EOCIE 位开转换完成中断。
- `ADC_SMPR2`：每 3 位控制一个通道的采样时间。
- `ADC_SQR3 / SQR1`：规则组的转换序列，Rank 1~16 依次填通道号，L 字段填序列长度。
- `ADC_DR`：16 位数据寄存器，低 12 位是转换结果。

CubeMX 点几下就把这些寄存器写好了，你只要知道背后发生了什么。

---

## 9.3 三种转换模式：单次、连续、扫描+注入

F103 的 ADC 有好几种工作模式，新手容易被名字绕晕。我按"你什么时候会用到它"来分：

### 9.3.1 单次转换（Single Conversion）

`HAL_ADC_Start()` 启动一次，转完就停，EOC 置位，CPU 读一次 DR，ADC 进入空闲。下一次想读再 `HAL_ADC_Start()`。

适用场景：你在主循环里 `HAL_Delay(100)` 一次、读一次电位器、打印一次，这种"慢悠悠"的应用。简单、好理解、代码量最少。

### 9.3.2 连续转换（Continuous Conversion）

ADC 转完一次自动开始下一次，不需要 CPU 再触发。DR 寄存器永远是最新值。

适用场景：你想用 DMA 后台不停地把采样值搬进数组，CPU 完全不参与触发。后面 9.6 节多通道 DMA 就是这个模式。

### 9.3.3 扫描模式 + 规则组/注入组

扫描模式（Scan Conversion Mode）是说：规则组里配了几个通道，ADC 就按 Rank 顺序依次转一遍，转完一组才停。它和连续模式可以叠加——扫描+连续，就变成"通道 0、1、0、1、0、1……"循环采样。

注入组（Injected Group）是规则组的"插队"版本，最多 4 个通道，配一个外部触发源（比如定时器溢出），可以在规则组转到一半时硬插进来。这个对新手来说完全用不上，我提一句是怕你在参考手册里看到这个词发懵。

| 模式组合 | 触发方式 | 适用场景 |
|---|---|---|
| 单次 + 不扫描 | CPU 调一次 Start | 手动读旋钮 |
| 连续 + 不扫描 | 启动后自动循环 | 单通道高速采样 + DMA |
| 连续 + 扫描 | 启动后按 Rank 循环 | 多通道同时采样 + DMA |
| 单次 + 扫描 | CPU 触发一次转完一组 | 一次性扫几个传感器 |

---

## 9.4 第一次实操：电位器 + 串口打印

OK，原理讲完了，我们来点真东西。

### 9.4.1 接线

拿一个 3296W 多圈电位器（或者任何 10K 电位器）：

- 引脚 1 → 3.3V
- 引脚 3 → GND
- 引脚 2（中间滑动端）→ **PA0**

![3296W 10K 多圈电位器实物](images/potentiometer-3296w.jpg)

这就是一个最简单的分压电路：旋钮拧到一头，PA0 就是 3.3V；拧到另一头，PA0 就是 GND；中间位置电压线性变化。

### 9.4.2 CubeMX 配置

1. 新建工程，选 STM32F103C8。
2. 左侧 `Analog → ADC1` 点进去，勾选 `Active`。
3. 在 `Parameter Settings` 里：
   - `Clock Prescaler` = PCLK2/4（72MHz/4 = 18MHz，注意 ADC 时钟不能超过 14MHz，所以这里要选 /6 或者手动改到 12MHz；F103 默认 SYSCLK=72MHz，PCLK2=72MHz，选 /6 得到 12MHz 是最稳的）。
   - `Resolution` = 12 bits。
   - `Data Alignment` = Right alignment（右对齐，DR 低 12 位是结果）。
   - `Scan Conversion Mode` = Disable（单通道先不开扫描）。
   - `Continuous Conversion Mode` = Disable（单次）。
   - `External Trigger Conversion Source` = Regular Conversion launched by software。
   - `Rank 1` 的 `Channel` = Channel 0，`Sampling Time` = 55.5 Cycles（我建议这里从 55.5 起步，别用默认 1.5）。
4. 在 Pinout 视图上点 PA0，选 `ADC1_IN0`。
5. 顺便把 USART1 打开（115200-8-N-1），因为我们要 printf 打印。
6. 生成代码。

![CubeMX ADC1 配置界面](images/cubemx-adc-config.jpg)

### 9.4.3 代码

CubeMX 生成的工程里，`main.c` 的 USER CODE 区加这几行：

```c
/* USER CODE BEGIN PV */
uint16_t adc_val = 0;
float voltage = 0.0f;
/* USER CODE END PV */

/* 主循环里： */
while (1)
{
  /* USER CODE END WHILE */

  HAL_ADC_Start(&hadc1);                  // 软件触发一次转换
  HAL_ADC_PollForConversion(&hadc1, 100); // 最多等 100ms
  adc_val = HAL_ADC_GetValue(&hadc1);     // 读 DR 低 12 位
  voltage = adc_val * 3.3f / 4095.0f;     // 换算成电压

  printf("ADC = %4d,  Voltage = %.3f V\r\n", adc_val, voltage);
  HAL_Delay(300);

  /* USER CODE BEGIN 3 */
}
```

这就是所谓的"单次转换三件套"：Start → PollForConversion → GetValue。`HAL_ADC_PollForConversion` 内部就是轮询 SR.EOC 位，超时时间到了返回 HAL_TIMEOUT。

完整工程见 `code/ch09_adc_single.c`。

### 9.4.4 验证

编译下载，打开串口助手。你应该看到：

```
ADC = 2047,  Voltage = 1.651 V
ADC = 2050,  Voltage = 1.653 V
ADC = 2045,  Voltage = 1.649 V
```

转动电位器，数值应该在 0~4095 之间平滑变化。拿万用表量 PA0 对 GND 的电压，和串口打印的电压对一下，误差应该在 ±0.02V 以内。

我第一次跑这个的时候，串口全是 0，查了半天——原来是 PA0 没在 CubeMX 里选成 `ADC1_IN0`，引脚还停留在 Reset 默认的浮空 GPIO 模式。HAL_ADC_Start 不会报错，但读出来的就是垃圾值。

> 提醒：CubeIDE 里 printf 要打浮点，记得在 Project → Settings → MCU Settings → Linker 加 `-u _printf_float`，否则会打印出 `?` 或者一串奇怪字符。详见 [第8章 串口通信](ch08_串口.md)。

---

## 9.5 换个传感器：光敏电阻和 NTC 热敏电阻

电位器是"手动输入"，但 ADC 真正好玩的地方是接各种传感器。我给你两种最常见的接法。

### 9.5.1 光敏电阻 GL5528

![GL5528 光敏电阻实物](images/photoresistor-gl5528.jpg)

光敏电阻的特性是：**亮 → 电阻小（几 KΩ），暗 → 电阻大（几 MΩ）**。单独一个电阻没法产生电压，所以必须和一个固定电阻分压。

接法：

```
3.3V ──[LDR GL5528]──┬── PA0
                      │
                     [10KΩ]
                      │
                     GND
```

光照强的时候，LDR 电阻小，分压点电压高；用手捂住 LDR，电压就降下去。这个电路你可以用来做"光线自动控制 LED"。

### 9.5.2 NTC 热敏电阻 10K

![MF58 玻封 NTC 热敏电阻](images/ntc-thermistor-mf58.jpg)

NTC 是负温度系数热敏电阻：温度越高，电阻越小。25°C 时标称 10KΩ，100°C 时大约降到 1KΩ 多一点。

接法：

```
3.3V ──[10KΩ 固定电阻]──┬── PA0
                          │
                         [NTC 10K]
                          │
                         GND
```

为什么把固定电阻放上面、NTC 放下面？因为 NTC 发热时电阻变小，这种接法下温度越高、分压点电压越低，和直觉一致；反过来也行，只是公式要取倒数。

读出来的电压换算成电阻：

```
R_ntc = R_fixed × V_adc / (V_ref - V_adc)
```

再用 Beta 公式反算温度：

```
1/T = 1/T0 + (1/B) × ln(R_ntc / R0)
```

其中 T0 = 298.15K（25°C），R0 = 10K，B = 3950。这个公式我就不展开了，网上有现成的代码片段，复制改个变量名就能用。

### 9.5.3 三种接法对比

![三种最常见的 ADC 前端分压接法](images/adc-voltage-divider-variants.svg)

选上下拉电阻的时候有个经验法则：**让被测电阻在量程中点时与另一臂阻值接近**。比如 NTC 在 25°C 是 10K，你就配 10K 固定电阻；在 50°C 时 NTC 降到约 4K，这时候分压点大约是 3.3×4/(10+4) ≈ 0.94V，还在中段，线性最好。

如果你配的固定电阻和被测电阻差 10 倍以上，分压曲线会非常靠近 0 或靠近 VCC，中间一大段都没灵敏度——这是我做光线检测实验时踩过的坑。

---

## 9.6 多通道 DMA：让 ADC 自己在后台跑

到这里你已经能读一个通道了。但实际项目里你经常要同时读好几个：比如一个旋钮 + 一路光敏 + 一路 NTC，三个通道一起采样。如果还用主循环轮询，代码会变成这样：

```c
while (1) {
  read_ch0();   // 阻塞 100ms
  read_ch1();   // 又阻塞 100ms
  read_ch2();   // 又阻塞 100ms
  printf(...);
}
```

既慢又占 CPU。正确做法是用 **扫描模式 + DMA**，让 ADC 自己循环转，DMA 自己把结果搬进数组，CPU 只在主循环里读数组就行。

### 9.6.1 CubeMX 配置

1. ADC1：
   - `Scan Conversion Mode` = Enable
   - `Continuous Conversion Mode` = Enable
   - `Number of Conversion` = 2（两个通道）
   - Rank 1：Channel 0, Sampling Time 55.5 Cycles
   - Rank 2：Channel 1, Sampling Time 55.5 Cycles
2. PA0 选 `ADC1_IN0`，PA1 选 `ADC1_IN1`。
3. 切到 ADC1 的 `DMA Settings` 标签，点 Add：
   - DMA Request = ADC1
   - Mode = Circular（循环模式）
   - Data Width：Peripheral = Half Word，Memory = Half Word（因为 ADC_DR 是 16 位）
4. NVIC 里确认 `DMA1 channel1 global interrupt` 是勾上的（HAL_ADC_Start_DMA 内部要用到）。

![ADC + DMA 连续采样的数据流](images/adc-dma-continuous-block.svg)

### 9.6.2 代码

```c
/* USER CODE BEGIN PV */
uint16_t adc_buf[2];   // 注意：这里用 uint16_t，因为 ADC_DR 是 16 位
/* USER CODE END PV */

/* USER CODE BEGIN 2 */
HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buf, 2);
/* USER CODE END 2 */

while (1)
{
  /* USER CODE END WHILE */
  printf("CH0=%4d (%.2fV)  CH1=%4d (%.2fV)\r\n",
         adc_buf[0], adc_buf[0]*3.3f/4095.0f,
         adc_buf[1], adc_buf[1]*3.3f/4095.0f);
  HAL_Delay(500);
  /* USER CODE BEGIN 3 */
}
```

完整工程见 `code/ch09_adc_multi_dma.c`。

这里有个我之前写错的地方：`adc_buf` 类型。HAL 函数签名是 `uint32_t*`，但实际 ADC_DR 只有 16 位有效，用 `uint16_t adc_buf[2]` 然后强转 `(uint32_t*)` 也能跑，只是内存布局上 DMA 按半字（2 字节）搬，两个通道正好填满 4 字节。你用 `uint32_t adc_buf[2]` 也完全 OK，只是每个元素占 4 字节、后 16 位是 0。我习惯用 `uint16_t`，因为和硬件数据宽度一致。

启动之后，ADC 就永远在后台转：CH0 → CH1 → CH0 → CH1……，DMA 自动把 DR 的值搬到 `adc_buf[0]`、`adc_buf[1]`。主循环里你读到的就是最新值，CPU 几乎零占用。这就是为什么后面讲 DMA 那一章要把它单拎出来。

---

## 9.7 参考电压和电源噪声：精度的真正瓶颈

我前面说过，V_ref 默认是 3.3V，从板子上的 VDDA 直接取。但你知道吗——这个 3.3V 本身就不准：

- 板子上的 3.3V 来自 AMS1117-3.3，负载调整率大约 ±1%，也就是 ±33mV。
- USB 供电的时候，5V 本身就可能在 4.7~5.2V 之间飘。
- DC-DC 开关电源的纹波会直接窜到 ADC 采样值上。

**实际测量中你会发现：哪怕输入短到 GND，ADC 也不是稳稳的 0，而是在 0~5 之间跳；输入接 3.3V，也不是稳稳的 4095，而是 4090~4095。** 这都是正常的。

要做精确测量，有几个常见做法：

1. **多次采样取平均**：连续读 16 次，去掉最大最小，取中间 14 次平均。对随机噪声特别有效。
2. **在 VDDA 引脚加 LC 滤波**：板子上通常已经有一个 10Ω 磁珠 + 10nF + 1μF 滤波网络，你换 STM32 最小系统板的时候看看 schematic。
3. **用内部 VREFINT 反算**：ADC1_IN17 是内部 1.20V 基准，你把它读出来，假设它是准的，反推 VDDA 实际是多少：

   ```
   VDDA_real = 1.20V × 4095 / adc_vrefint
   ```

   然后所有其他通道的电压都用这个实际 VDDA 去换算，精度能提一大截。
4. **外接精密基准源**：比如 TL431（2.5V 基准，精度 1%）直接接 VREF+ 引脚。但 Blue Pill 把 VREF+ 接到 VDDA 了，要改 PCB 才行，初学者不用折腾。

---

## 9.8 常见坑与调试思路

我把这一章踩到的、和被人问过的坑集中列一下：

**1. 读出来永远是 0 或者 4095。**
八成是接线问题：引脚悬空、或者电位器没接中间滑动端。先拿万用表量一下 PA0 对 GND 的电压，确认硬件上确实有 0~3.3V 变化，再去查软件。

**2. 数值乱跳，跳 100~200。**
前端阻抗太高，采样时间不够。把 Sampling Time 从 1.5 改成 55.5 或者 71.5。还跳的话，在 PA0 对 GND 加一个 100nF 电容，做硬件低通滤波。

**3. 多通道时 adc_buf[0] 和 adc_buf[1] 对不上。**
检查 CubeMX 里 Rank 的顺序。DMA 是按 Rank 1、Rank 2、Rank 3……的顺序往数组里搬的，Rank 1 的结果永远在 `adc_buf[0]`，不管你物理上把哪个传感器接到了哪个通道。我有一次把 Rank 1 设成 IN1、Rank 2 设成 IN0，结果数组里 CH0 的数据其实是 PA1 的，花了半小时才反应过来。

**4. 浮点 printf 打印 `?`。**
CubeIDE 没加 `-u _printf_float` 链接选项。

**5. 接了光敏电阻但读到的范围特别窄。**
分压电阻取值不对。亮时 LDR 是 5K，暗时是 500K，如果你配了 10K 下拉，亮时分压点约 2.4V，暗时约 3.23V——变化范围才 0.8V。这种应用要么换更大的下拉电阻（比如 100K），要么加一级运放缓冲。

**6. HAL_ADC_Start_DMA 之后再想手动读一次单次值。**
会冲突。DMA 模式启动后，ADC 一直在连续转，你不能再调用 `HAL_ADC_PollForConversion`。要切换模式，先 `HAL_ADC_Stop_DMA`，再 Start。

**7. ADC 刚上电第一次读出来的值总是偏大。**
这是 F103 的经典问题：ADC 内部校准没做。CubeMX 生成代码时默认会在 `MX_ADC1_Init()` 之后调一次 `HAL_ADCEx_Calibration_Start(&hadc1)`，但如果你是手搓寄存器或者老版本 HAL，这一步可能漏了。校准的原理是让 ADC 自己短接内部参考、测一次偏差、存到校准寄存器。手动加一句：

```c
HAL_ADC_DeInit(&hadc1);
MX_ADC1_Init();
HAL_ADCEx_Calibration_Start(&hadc1);
```

校准完再启动，数据会稳很多。

**8. 用中断方式采 ADC，回调里读不到新值。**
`HAL_ADC_ConvCpltCallback` 是转换完成中断回调，里面你要做的是 `HAL_ADC_GetValue()` 把 DR 读走，否则 EOC 标志没清掉，下一次转换就停了。HAL 库内部其实已经帮你读了，但如果你自己注册了回调，最好读一次确保清标志。

---

## 9.9 中断方式：不用 DMA 也能解放 CPU

在讲 DMA 之前，我得先提一下中断方式，因为它是 DMA 的"前身"，理解了它你才知道 DMA 为什么好用。

CubeMX 里 ADC1 勾上 `End of Conversion Interrupt`，然后：

```c
/* USER CODE BEGIN 2 */
HAL_ADC_Start_IT(&hadc1);   // 启动一次中断转换
/* USER CODE END 2 */

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
  if (hadc->Instance == ADC1) {
    adc_val = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Start_IT(&hadc1);   // 再触发下一次
  }
}
```

这样每次转换完进一次中断，CPU 在中断里读一次值就走。问题是：**每次转换都要进一次中断、保存现场、读 DR、恢复现场**。如果你 1ms 转一次，中断还算轻；但如果你想 10kHz 采样，中断就把 CPU 吃光了——这时候 DMA 就是必需品。

DMA 和中断的本质区别是：

- **中断**：每搬一个元素，CPU 进一次中断。
- **DMA**：搬 N 个元素，CPU 只在搬完 N 个的时候进一次中断（或者根本不进，循环模式一直跑）。

这就是为什么高速 ADC、连续串口接收都必须用 DMA。

---

## 9.10 内部温度传感器和 VREFINT：板子自带的两路"免费"通道

F103 的 ADC1 还藏了两路内部通道，不用外接任何元件：

- **ADC1_IN16**：内部温度传感器。它测的是芯片结温，不是环境温度。校准公式在 datasheet 里：`Temperature (°C) = (Vsense - V25) / Avg_Slope + 25`，其中 V25 典型值 1.43V，Avg_Slope 4.3 mV/°C。
- **ADC1_IN17**：内部 VREFINT，约 1.20V。前面 9.7 节讲过，用它反算 VDDA 实际电压。

CubeMX 里勾上 `Temperature Sensor Channel` 和 `VREFINT Channel` 就启用了。读出来的值换算一下，你能看到芯片当前温度——我用手按住芯片，过 10 秒温度能升 3~5°C，挺好玩的。

---

## 9.11 软件滤波：让 ADC 数据真正"能用"

ADC 原始数据几乎从来不能直接用。哪怕你硬件做得再好，电源纹波、接触抖动、外界干扰都会让读数上下跳。我做项目里常用的几种软件滤波，按复杂度排：

**1. 算术平均**：连续读 N 次，加起来除以 N。简单粗暴，对随机噪声有效，对周期性干扰（比如开关电源的 50kHz 纹波）效果一般。

```c
uint32_t sum = 0;
for (int i = 0; i < 16; i++) {
  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, 100);
  sum += HAL_ADC_GetValue(&hadc1);
}
uint16_t avg = sum / 16;
```

**2. 滑动平均**：维护一个环形 buffer，每次新数据进来替换最老的，然后取平均。比算术平均响应快，因为你不用等凑够 16 次再算。

**3. 中值滤波**：连续读 5 次，排序取中间值。对"突然跳一下"的毛刺特别有效，比如你手碰了一下引脚、或者旁边电机启动瞬间的干扰。

**4. 一阶滞后滤波（低通）**：

```c
filtered = filtered * 0.8 + new_value * 0.2;
```

系数越小越平滑，但响应越慢。这种滤波在 8 位机上也算经典，Cortex-M3 跑起来毫无压力。

我自己的组合是：**硬件 100nF 电容 + 滑动平均 8 点 + 中值滤波 5 点**。一般工业现场的传感器数据这么处理完，波动能压到 ±2 个 LSB 以内。

---

## 9.12 本章小结

- ADC 把模拟电压切成 0~4095 的数字，1 LSB ≈ 0.8 mV（3.3V 参考）。
- SAR 型 ADC 一次转换 = 采样（SMPR 决定时间）+ 12.5 周期逐次比较 + 写 DR。
- 单通道三件套：`HAL_ADC_Start` → `HAL_ADC_PollForConversion` → `HAL_ADC_GetValue`。
- 多通道用扫描 + 连续 + DMA，`HAL_ADC_Start_DMA` 一次启动，后台自动采样。
- 分压电路的电阻取值要和被测电阻在同一量级，灵敏度才够。
- 参考电压本身有误差，多次平均 + 内部 VREFINT 校算是性价比最高的提升手段。
- 下一章我们专门讲 DMA——为什么 ADC 一接 DMA 就这么丝滑，它在后台到底是怎么搬数据的。

---

**自测题**：

1. 3.3V 参考、12 位 ADC，输入 2.0V，理论上读到的数字是多少？
2. 为什么采样时间默认 1.5 周期在接光敏电阻时不够？
3. 多通道 DMA 时，`adc_buf[0]` 对应的是 Rank 1 还是 Rank 2 的结果？
4. 想测环境温度，NTC 分压电路里上下臂电阻怎么选？
5. ADC 短接 GND 时读到 0~5 之间跳，是 ADC 坏了吗？怎么解释？

---

<!-- ===== 第9章 完 ===== -->

下一章：[第10章 DMA 数据传输](ch10_DMA.md)
