# 第5章 按键输入与外部中断 EXTI

> 写到这一章，我心里其实是有点兴奋的。前面四章我们一直在让板子"输出"——点亮 LED、跑时钟、配置串口。这一章开始，板子要"听"我们说话了：按一下按键，灯亮；再按一下，灯灭。听起来像个玩具，但背后那套 **EXTI 外部中断 + NVIC 中断控制器** 的机制，是后面所有"事件驱动"玩法的地基。

我先把结论放这儿：按键这件事有两种做法——轮询和中断。轮询是死办法，能跑通但占 CPU；中断才是嵌入式真正的玩法。这一章我会把按键电路、抖动、NVIC 优先级、HAL 回调的"三层结构"全部拆开揉碎给你看，最后再讲长按/短按这种稍微进阶一点的状态机写法。

---

## 5.1 先把按键这件事说清楚

### 5.1.1 一个按键到底长什么样

![面包板轻触按键实物图](images/breadboard-button.jpg)

![轻触开关内部结构分解图](images/tactile-switch-exploded.jpg)

我们平时用的那种 6×6mm、四脚的小方块叫 **轻触开关（Tactile Switch）**。它内部其实就是两片金属弹片：没按的时候，两脚之间是断开的；按下的时候，金属弹片被压下去，导通；松开又弹回来。四脚不是四路独立——拆开看，1 和 2 内部是连在一起的，3 和 4 也是连在一起的，其实就相当于两脚开关，另外两脚只是为了插在面包板上更稳。

![四脚轻触开关引脚导通关系](images/tactile-switch-pinout.jpg)

接线的时候只要任意选一对（比如 1、3）接到电路里就行，另一对悬空不影响。我刚开始玩的时候总疑惑：为什么四根脚？是不是每根都要接？后来拆开一个坏掉的看才明白。

### 5.1.2 上拉 / 下拉：引脚凭什么知道"按没按"

![按键上拉/下拉电路示意图](images/button-circuit.svg)

按键本身只是一个开关，它自己不产生电平。要让 MCU 的 GPIO 能读出一个确定的 0 或 1，必须在引脚和电源/地之间加一个电阻，把"没按"时的电平拉到一个确定值。

最常见的接法是这样的：

```
        3.3V
         │
        [R]  上拉电阻（10kΩ 最常用）
         │
  引脚 ──┼── 按键 ── GND
```

- **没按**：按键断开，电阻把引脚拉到 3.3V → 读到高电平（`GPIO_PIN_SET`）。
- **按下**：按键导通，引脚直接接地 → 读到低电平（`GPIO_PIN_RESET`）。

反过来也可以——按键接 3.3V，电阻下拉到 GND，那就是"按下=高电平"。两种都能用，**逻辑相反而已**。我个人习惯用"按键接地 + 上拉"，因为板子上 GND 多、走线短，而且 STM32 的 GPIO 内部已经集成了上拉电阻，外部一颗电阻都不用加。

读引脚的 HAL 函数就一个：

```c
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
// 返回值是 GPIO_PIN_SET(1) 或 GPIO_PIN_RESET(0)
```

就这么简单。问题不在"怎么读"，而在"什么时候读"。

### 5.1.3 轮询：能跑，但很蠢

最朴素的写法就是在主循环里不停地读：

```c
while (1) {
  if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);  // 亮
  } else {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);    // 灭
  }
}
```

我第一次写的就是这种。跑起来也确实工作——按下去灯亮，松开灯灭。但你很快会遇到两个问题：

1. **CPU 死循环空转**。72MHz 在跑，什么正事都没干，就一直在 `ReadPin`。这在嵌入式里是浪费。
2. **如果主循环里有别的耗时操作**，比如 `HAL_Delay(500)`、比如跑一段算法、比如等串口数据，那按键就"来不及响应"——你按了一下，灯可能要过几百毫秒才亮。

这就是轮询的根本毛病：**CPU 不知道你什么时候按，只能不停地问**。

### 5.1.4 中断：让硬件来叫我们

中断的思路完全反过来。我们不主动去问，而是告诉 MCU："PA0 这个引脚上，一旦出现高→低的跳变，你就停下来去叫我。"然后 CPU 就可以安心去干别的事，事件来了硬件自动打断它。

整个硬件链条是这样的：

```
引脚电平出现边沿（上升沿 / 下降沿）
   └─► EXTI 控制器检测到这个边沿
        └─► 在 EXTI 挂起寄存器（PR）上置一个标志
             └─► NVIC 把这个事件转成一个中断号
                  └─► Cortex-M 内核暂停当前程序
                       └─► 跳转到对应的中断服务函数 ISR
                            └─► 我们写的回调被执行
                                 └─► 执行完回到断点，继续原程序
```

![EXTI 中断响应流程：外部事件→边沿检测→NVIC→ISR](images/exti-flow.svg)

这个流程听起来复杂，但好处是：**平时 CPU 跑自己的，事件来了自动响应，时间精度是微秒级**。

---

## 5.2 实操一：轮询版（10 分钟跑起来）

我们先把最简单的版本做出来，建立手感。

### 5.2.1 CubeMX 配置

打开之前的工程，做这几件事：

1. **PC13** → 已经是 `GPIO_Output`（接板载 LED），不用动。
2. **PA0** → 点一下，选 `GPIO_Input`。
3. 切到中间的 `GPIO` 标签页，点 PA0 那一行，把 `GPIO Pull-up/Pull-down` 改成 `Pull-up`（内部上拉，配合按键接地用）。
4. 重新生成代码。

> 这里我要提醒一句：内部上拉的阻值大概在 30~50kΩ，比外部 10kΩ 大一点。日常用完全够，但如果你的按键线很长、或者环境干扰大，还是建议外部加一颗 10kΩ 上拉。

### 5.2.2 代码

完整代码在 `code/ch05_button_poll.c`，核心就是这几行：

```c
/* USER CODE BEGIN 2 */
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);  /* 初始熄灭 */
/* USER CODE END 2 */

while (1)
{
  /* USER CODE END WHILE */

  /* 读 PA0：按下 → 低电平 → 点亮 */
  if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET)
  {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
  }
  else
  {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
  }

  /* USER CODE BEGIN 3 */
}
```

烧进去，按一下灯亮、松开灯灭。到这里你已经"会读按键"了。

但我希望你停下来想一个问题：**如果我把这个 while(1) 里面加一行 `HAL_Delay(200)`，再按按键，你会发现灯要过 200ms 才有反应**。这就是轮询的代价。我们下面要做中断版，就是为了消灭这种延迟。

---

## 5.3 实操二：外部中断版（按键按下翻一次 LED）

目标改一下：**按一下按键，LED 翻转一次**（再按一下又翻回来）。这才是真正有"交互"的玩法。

### 5.3.1 CubeMX 配置（比轮询多三步）

1. 点 **PA0**，这次选 `GPIO_EXTI0`。
2. 切到 `GPIO` 标签页，点 PA0 那一行：
   - `GPIO mode` 选 `External Interrupt Mode with Falling edge trigger detection`（下降沿触发——也就是高→低跳变的瞬间，正好是"按下"的时刻）。
   - `GPIO Pull-up/Pull-down` 还是 `Pull-up`。
3. 左侧树展开 `System Core → NVIC`，勾上 `EXTI line0 interrupt`。优先级保持默认就行。
4. 重新生成代码。

CubeMX 帮你做了几件事：
- 在 GPIO 初始化里把 PA0 配成 EXTI 模式；
- 在 NVIC 里使能了 EXTI0 中断；
- 在 `stm32f1xx_it.c` 里生成了 `EXTI0_IRQHandler` 中断入口函数。

### 5.3.2 代码：只写最内层的回调

代码在 `code/ch05_button_exti.c`，其实就一个函数：

```c
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

烧进去，按一下灯翻一次。主循环 `while(1)` 里我什么都没写——它就是空的。但你按按键，灯照样翻。**这就是中断的威力：主循环什么都不用干，事件来了自动响应。**

### 5.3.3 为什么是三层函数？我第一次看懵了

你打开 `stm32f1xx_it.c`，会看到这个函数：

```c
void EXTI0_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}
```

然后打开 `stm32f1xx_hal_gpio.c`，里面是：

```c
void HAL_GPIO_EXTI_IRQHandler(uint16_t GPIO_Pin)
{
  if (__HAL_GPIO_EXTI_GET_IT(GPIO_Pin) != RESET)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_Pin);   /* 关键：清除挂起标志 */
    HAL_GPIO_EXTI_Callback(GPIO_Pin);     /* 调你写的回调 */
  }
}
```

再往下就是你写的 `HAL_GPIO_EXTI_Callback`。

这就是 HAL 的三层中断结构：

```
EXTI0_IRQHandler         ← 硬件中断入口（向量表里的地址），CubeMX 生成
   └─► HAL_GPIO_EXTI_IRQHandler   ← HAL 库封装：判断+清标志+调回调
        └─► HAL_GPIO_EXTI_Callback ← 你写的业务逻辑
```

**你只需要写最内层的 Callback。** 清标志这件事，HAL 已经帮你做了。如果你不写 Callback，中断照样会被响应、标志照样会被清，只是没人干活而已。

这个模式后面所有外设都一样：定时器中断、串口接收中断、ADC 转换完成中断……都是"在 Callback 里写逻辑"。

---

## 5.4 深入：EXTI 到底是怎么回事

讲到这儿，你应该已经能跑起来了。但我希望你往下再挖一层——这章是讲"事件驱动"的地基，搞懂寄存器级的东西，后面调 bug 才不会抓瞎。

### 5.4.1 EXTI 的 20 条线，不是你想用就能用

F103 的 EXTI 控制器一共 20 条线（EXTI0~EXTI19），但**引脚分配有个硬规则**：

![EXTI 线分配：同号引脚共享一条 EXTI 线](images/exti-line-sharing.svg)

- **EXTI0** 可以接 PA0、PB0、PC0、PD0……**但同一时刻只能选一个**。
- **EXTI1** 可以接 PA1、PB1、PC1……同样只能选一个。
- **EXTI2、EXTI3、EXTI4** 同理。
- **EXTI5~EXTI9 共用一条中断向量**（`EXTI9_5_IRQHandler`），意味着这几条线触发时会进同一个 ISR，你要在 Callback 里用 `GPIO_Pin` 参数区分是哪一路。
- **EXTI10~EXTI15** 也共用一条（`EXTI15_10_IRQHandler`）。

"选哪一路"这件事由 **AFIO 外部中断配置寄存器（EXTICR1~4）** 决定。CubeMX 你选 `GPIO_EXTI0` 的时候，它自动写好了：把 PA0 接到 EXTI0 线上。如果你想换 PB0，那在 CubeMX 里重新选一下，它会改 AFIO 的配置。

> 我见过新手踩坑：PA0 配了 EXTI0，然后又想把 PB0 也配成外部中断——编译能过，但烧进去根本不触发。因为 EXTI0 已经被 PA0"占"了，PB0 接不上。

### 5.4.2 关键寄存器：IMR / RTSR / FTSR / PR

EXTI 对外设的控制就四个核心寄存器（每个 32 位，一位对应一条线）：

| 寄存器 | 作用 | 什么时候写 |
|---|---|---|
| **IMR**（Interrupt Mask） | 中断屏蔽，某位写 1 表示允许该线触发中断 | CubeMX 自动写 |
| **RTSR**（Rising Trigger） | 上升沿触发使能，写 1 表示高→低跳变要响应 | CubeMX 自动写 |
| **FTSR**（Falling Trigger） | 下降沿触发使能，写 1 表示低→高跳变要响应 | CubeMX 自动写 |
| **PR**（Pending Register） | 挂起标志，硬件自动置位；软件写 1 清除 | HAL 自动清 |

你在 CubeMX 里选"Falling edge trigger"，本质上就是往 FTSR 的第 0 位写 1、RTSR 的第 0 位写 0。选"Rising edge"就反过来。选"Rising/Falling"就是两个都写 1——按下和松开都会触发中断。

我为什么要讲这些？因为**将来你调 bug 的时候，会去看寄存器**。比如中断不触发，你打开调试器看 `EXTI->IMR`、`EXTI->FTSR`、`EXTI->PR` 这三个寄存器，一眼就能看出问题在哪。

### 5.4.3 NVIC：优先级的"嵌套"游戏

![NVIC 优先级分组：抢占优先级 vs 响应优先级](images/nvic-priority-group.svg)

NVIC 是 Cortex-M 内核的一部分，不是 ST 加的外设。它管的是：**当多个中断同时来的时候，先响应谁？响应一个的过程中，能不能被另一个打断？**

每个中断都有一个优先级值，分成两部分：

- **抢占优先级（Preemption Priority）**：决定能不能打断别人。数值小的可以打断数值大的。
- **响应优先级 / 子优先级（Sub Priority）**：当两个中断抢占级相同、同时挂起时，先响应谁。**子优先级不能打断正在跑的中断**。

CubeMX 默认用的是 `NVIC_PRIORITYGROUP_4`——4 位全部给抢占优先级，0~15 共 16 级，子优先级为 0。对入门来说，你只要记住：**数值越小优先级越高**。

举个例子：EXTI0 我设成抢占级 1，TIM2 设成抢占级 3。那 EXTI0 触发的时候，哪怕 TIM2 正在中断服务里，也会被 EXTI0 打断；等 EXTI0 跑完，再回来继续 TIM2。这就是"嵌套"。

> 我自己写代码时的习惯：外部中断和定时器都给默认值就行（都是 3），**真要调优先级，一般是串口接收和某个高频中断冲突了，才把串口调高**。入门别上来就调优先级，容易绕晕。

---

## 5.5 按键抖动：这个坑几乎人人都踩

### 5.5.1 抖动长什么样

![按键抖动波形与软件消抖示意](images/debounce-waveform.svg)

机械按键按下的时候，金属弹片不是一次就稳稳压下去的——它会在那里弹跳几下，接触电阻反复跳变。结果就是：**你以为按了一下，实际电平在 5~20ms 内高低跳了十几次**。

如果我们用下降沿中断，那这一次按下可能会触发十几次中断，LED 就会"乱闪"——按一下翻好几下。我第一次写中断版的时候就遇到了，当时以为是代码错了，调了半天。

### 5.5.2 软件消抖：最常用的做法

软件消抖的思路很简单：**检测到下降沿后，别急着处理，先延时 20ms 再读一次引脚，如果还是低电平，才认定是真按下**。

最简单的写法：

```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == GPIO_PIN_0)
  {
    HAL_Delay(20);                              // 等 20ms 抖动过去
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET)
    {
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
  }
}
```

> 注意：**这种写法在中断里调 `HAL_Delay`，我不推荐**。`HAL_Delay` 靠 SysTick 累加，而中断里如果优先级比 SysTick 高，SysTick 永远不被响应，直接卡死。这里只是演示原理，实际项目里别这么写。

正确的做法是用定时器扫描——这就是第 6 章要讲的内容。简单说：把定时器配成每 10ms 进一次中断，在里面读一次按键电平，连续读到 2 次低电平（即 20ms），才认定"按下"。这种做法不依赖延时函数，也不会在中断里卡 CPU。

### 5.5.3 硬件消抖：RC 滤波

如果你想硬件搞定，就在按键两端加一个 RC 滤波：

```
引脚 ──┬── 10kΩ ── 按键 ── GND
       │
       └── 0.1µF ── GND
```

电容把尖峰抖动平滑掉，硬件输出就是干净的波形。这种做法可靠性高，但要多一颗电阻一颗电容。我自己做产品时更喜欢硬件消抖，做原型 demo 时用软件扫描。

---

## 5.6 进阶：长按、短按、多按键

### 5.6.1 状态机：一个按键识别三种动作

![按键状态机：短按 / 长按识别](images/key-state-machine.svg)

真实项目里，一个按键往往要承担好几个功能：短按、长按、双击。怎么识别？靠状态机。

思路是：定时器每 10ms 扫一次引脚，在状态机里走。我给你一个最小骨架（伪代码）：

```c
typedef enum {
  KEY_IDLE,
  KEY_PRESSED,      /* 已确认按下，开始计时 */
  KEY_LONG_FIRED,   /* 已经触发过长按事件 */
} key_state_t;

key_state_t state = KEY_IDLE;
uint16_t hold_ticks = 0;   /* 10ms 为单位 */
uint8_t  event = 0;        /* 事件标志：1=短按，2=长按 */

/* 在定时器 10ms 中断里调用 */
void key_scan(void)
{
  GPIO_PinState s = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);

  switch (state)
  {
  case KEY_IDLE:
    if (s == GPIO_PIN_RESET) {          /* 看到低电平 */
      hold_ticks++;
      if (hold_ticks >= 2) {            /* 连续 20ms 都低 → 真按下 */
        state = KEY_PRESSED;
        hold_ticks = 0;
      }
    } else {
      hold_ticks = 0;
    }
    break;

  case KEY_PRESSED:
    if (s == GPIO_PIN_RESET) {
      hold_ticks++;
      if (hold_ticks >= 100) {          /* 按住超过 1s */
        event = 2;                       /* 长按事件 */
        state = KEY_LONG_FIRED;
      }
    } else {                             /* 松手了 */
      if (hold_ticks < 100) event = 1;   /* 短按事件 */
      state = KEY_IDLE;
      hold_ticks = 0;
    }
    break;

  case KEY_LONG_FIRED:
    if (s == GPIO_PIN_SET) {             /* 松手才回 IDLE */
      state = KEY_IDLE;
      hold_ticks = 0;
    }
    break;
  }
}
```

主循环里看到 `event != 0` 就干活，干完清 0。这种写法的好处是：**中断里只扫描、置标志，真正的动作全在主循环里**，不会卡住中断。

### 5.6.2 多个按键怎么办

如果你有三个按键，最直接的做法是各占一个 EXTI 线：

- 按键 1 → PA0 → EXTI0
- 按键 2 → PB1 → EXTI1
- 按键 3 → PA2 → EXTI2

三条线、三个独立中断向量，互不干扰。回调函数里用 `if (GPIO_Pin == GPIO_PIN_x)` 区分。

但如果按键很多（比如 8 个），EXTI 线不够用怎么办？两个办法：

1. **矩阵键盘**：行列扫描，用 8 个 GPIO 读 16 个按键（4×4 矩阵）。这个以后单独讲。
2. **共用一个 EXTI 线 + 轮询**：比如所有按键都通过二极管或门接到 PA0，任何一个按下都触发 EXTI0 中断，进中断后再去轮询所有 GPIO 看是哪个键。这种省中断线，但逻辑稍复杂。

---

## 5.7 轮询 vs 中断：什么时候用哪个

我把自己的经验列一下，你可以参考：

| 场景 | 推荐方式 | 理由 |
|---|---|---|
| 简单 demo、教学 | 轮询 | 代码少，一眼看懂 |
| 按键需要实时响应 | 中断 + 定时器扫描 | 主循环不耽误，按键不丢 |
| 主循环里有大量阻塞操作（延时、串口等待） | 中断 | 否则按键会"失灵" |
| 按键数量多（≥4） | 矩阵扫描，不用中断 | 省引脚，逻辑统一 |
| 极低功耗场景 | 中断唤醒 | 平时休眠，按键按下才醒来 |

**我的建议**：入门先把轮询写明白，再写中断版对比一次。体会过"主循环卡死、按键失灵"的痛苦，你才会真正理解中断的价值。

---

## 5.8 直接读寄存器：不依赖 HAL 也能配 EXTI

如果你想彻底搞懂，我带你看一遍"裸寄存器版"的 EXTI 配置。CubeMX 生成的 HAL 代码本质上就是在写这些寄存器，只不过包了一层函数。

假设我要让 PA0 做下降沿外部中断：

```c
/* 1. 使能 GPIOA 和 AFIO 时钟 */
RCC->APB2ENR |= RCC_APB2ENR_GPIOAEN;
RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;

/* 2. 把 PA0 配成浮空输入（EXTI 模式下输入缓冲始终开启） */
GPIOA->CRL &= ~(0xF << 0);   /* 清掉 PA0 的配置位 */
GPIOA->CRL |= (0x4 << 0);    /* 0100 = 浮空输入 */

/* 3. 用 AFIO_EXTICR1 把 EXTI0 线映射到 PA0（默认就是 PA0，这步其实可省） */
AFIO->EXTICR[0] &= ~(0xF << 0);   /* EXTI0 选 0 = PA0 */

/* 4. 允许 EXTI0 中断（IMR 第 0 位置 1） */
EXTI->IMR  |= (1 << 0);

/* 5. 配置下降沿触发（FTSR 第 0 位置 1） */
EXTI->FTSR |= (1 << 0);

/* 6. 配置 NVIC：使能 EXTI0 中断、设优先级 */
NVIC_SetPriority(EXTI0_IRQn, 3);
NVIC_EnableIRQ(EXTI0_IRQn);
```

这段代码跑起来效果和 CubeMX 配的一样。我为什么要贴这个？因为**以后你看别人的代码、或者调试时卡壳了，打开寄存器手册对照着看，就知道每一步在干什么**。

### 5.8.1 AFIO 重映射是什么

AFIO（Alternate Function IO，复用功能 IO）有两个职责：

1. **EXTI 线选择**（就是上面那个 `EXTICR` 寄存器）——决定 EXTI0~15 分别接到哪个 GPIO 端口。
2. **外设引脚重映射**——比如你把 SPI1 的 SCK 默认在 PA5，通过 AFIO 可以重映射到 PB3。

入门阶段你不需要手动配 AFIO，CubeMX 选 GPIO_EXTIx 时自动处理了。但你要知道有这么个东西，不然哪天看到 `__HAL_AFIO_REMAP_SWJ_NOJTAG()` 这种宏会懵。

## 5.9 常见坑（我都踩过）

1. **中断不触发**：先检查 CubeMX 里 NVIC 那栏有没有勾 `EXTI line0 interrupt`。我第一次忘勾，编译过、烧进去按按键没反应，查了俩小时。
2. **触发边沿反了**：按键接地 + 上拉，按下是高→低，应该用 **Falling edge**。如果你选了 Rising edge，那是松开才触发。
3. **一次按键翻好几下**：抖动没消。用定时器扫描，别在中断里加 `HAL_Delay`。
4. **回调里加 `HAL_Delay` 卡死**：中断里永远别调用依赖 SysTick 的函数。真要延时，用定时器扫描的方式做。
5. **EXTI 线冲突**：PA0 用了 EXTI0，PB0 就别想再接外部中断了。换 PB1（EXTI1）。
6. **回调没被调用**：检查 `EXTI0_IRQHandler` 里是不是漏了 `HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0)`（CubeMX 自动生成，别手滑删了）；确认你的 Callback 写在 `USER CODE BEGIN 4` 区段里，别被下次生成覆盖。
7. **读按键电平反了**：上拉接地应该是"按下=低"。如果你搞反了，先画电路图，别凭记忆写代码。

---

## 5.10 本章小结

> 写这一章的时候我反复在想：新手第一次接触中断，最容易卡在"为什么要这么多层函数"上。所以我特意把三层结构拆开讲了。你以后看任何 STM32 工程，遇到中断函数都可以按这个套路找：先找硬件入口（xxx_IRQHandler），再看 HAL 帮你做了什么（清标志），最后写自己的 Callback。

- 按键就是一个开关，靠上拉/下拉电阻把空闲电平定到一个确定值。
- 轮询简单但占 CPU、响应慢；中断实时但要处理抖动和优先级。
- EXTI 是引脚→中断的桥梁，同号引脚共享一条线（EXTI0~4 独立，5~9、10~15 共用）。
- NVIC 管优先级：抢占级决定能不能嵌套，子级决定同级先响应谁。
- HAL 的中断是三层结构：`IRQHandler` → `HAL_xxx_IRQHandler` → `Callback`，你只写 Callback。
- 消抖推荐用定时器扫描（下一章讲完定时器你就懂了），别在中断里 `HAL_Delay`。
- 寄存器级看：IMR 允许中断、RTSR/FTSR 选边沿、PR 是挂起标志，HAL 全包了，但调试时你得看得懂。

**自测题：**
1. 按键接地 + 内部上拉时，按下读到的电平是高还是低？换一种接法（按键接 3.3V）逻辑会怎样？
2. 我能不能同时把 PA0 和 PB0 都配成 EXTI0？为什么？
3. `HAL_GPIO_EXTI_Callback` 里为什么不用手动清标志位？谁清了？
4. 抢占优先级 2 和子优先级 5，哪个数字代表"更优先"？抢占级 2 能不能打断抢占级 3？
5. 中断里调用 `HAL_Delay(100)` 为什么会卡死？
6. 一个 4×4 矩阵键盘需要几个 GPIO 引脚？为什么不用 16 个独立 EXTI？

下一章我们把定时器玩起来——到时候你会发现，按键消抖、状态机、周期任务，全靠定时器在背后打节拍。

---

> 写到这里，第 5 章就结束了。建议你动手做一遍：先写轮询版，再写中断版，对比一下两者在主循环里加 `HAL_Delay` 时的反应。这个对比会让你对"中断"两个字有直观感受。

---

<!-- ===== 第5章 完 ===== -->
