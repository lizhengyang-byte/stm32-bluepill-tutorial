# 第11章 I2C / SPI 通信（OLED 实战）

> 前面十章我们玩的都是 STM32 自己身边的东西：GPIO、定时器、ADC、串口、DMA。从这一章开始，STM32 要"开口说话"了——不是对电脑，而是对外面挂的传感器、显示屏、存储芯片。我第一次把 I2C 的 OLED 点亮的时候，盯着那块小屏幕上跳出的 "Hello" 看了半天，比点亮 LED 还激动。

这一章我们要聊两种最常用的串行总线：I2C 和 SPI。为什么要一次聊两种？因为它们俩干的是同一件事——"把数据从 A 搬到 B"——但思路完全不同，放一起对比着学，你才知道什么时候该用哪个。最后我们用 SSD1306 这块 0.96 寸的小 OLED 做实战，把 I2C 从接线、CubeMX 配置、到底层寄存器，全捋一遍。

代码在 [code/ch11_oled_i2c.c](code/ch11_oled_i2c.c)。

---

## 11.1 先搞清楚：为什么需要串行总线

你回想一下，[第 8 章串口](ch08_串口.md)我们已经能让 STM32 和电脑"说话"了，一根 TX 一根 RX，靠 UART 协议。那为什么还要 I2C 和 SPI？

因为 UART 是"点对点"的——一根线只能连一个设备，而且双方得事先约好波特率。你现在如果要同时挂一个温湿度传感器、一个气压计、一块 OLED 在 STM32 旁边，难道要开三个串口？F103C8T6 总共就三个 USART，早不够用了。

串行总线的思路是：**所有设备共享同一组线**，主机发数据的时候带上"地址"，只有被点名的设备才应答。这样一根 SCL + 一根 SDA 理论上能挂 128 个设备（7 位地址），引脚省、接线简单。

I2C 和 SPI 就是两种这样的总线。区别在于：

- I2C 只用两根线，半双工，速度慢一点（标准 100k、快速 400k），但接线省、支持多主机。
- SPI 用四根线，全双工，速度快得多（几十 MHz），但每个从机要独占一根片选线，设备多了线就多。

我做项目的时候，传感器（温湿度、气压、OLED）几乎全走 I2C；高速设备（Flash、TFT 彩屏、SD 卡）走 SPI。这个分工你记一下，后面选型就不会纠结。

---

## 11.2 I2C 协议：两根线是怎么把话讲明白的

### 11.2.1 物理层：开漏 + 上拉

I2C 的 SCL 和 SDA 都是**开漏输出**（open-drain），意思是设备只能把线拉低到 GND，不能主动推到 3.3V。要让线变高，得靠外部的上拉电阻把它"拉"回 3.3V。

![I2C 两线总线连接：主设备与多个从设备（SCL/SDA+上拉）](images/i2c-wiring.svg)

为什么要这样设计？因为这样天然支持"线与"——任何一个设备拉低，整条线就是低的，不需要仲裁。你挂三个传感器在同一根 SDA 上，谁都不会把另外两个"顶"坏。

上拉电阻典型值 4.7kΩ，接 3.3V。很多模块（比如 OLED、BMP280）板子上已经焊了上拉，你不用自己加；但如果你拿裸芯片飞线，就得自己焊。Blue Pill 本身在 PB6/PB7 上没带上拉，模块自己带的就够了。

### 11.2.2 起始条件和停止条件

I2C 总线上平时 SCL 和 SDA 都是高的（被上拉电阻拉着）。一次传输从一个**起始条件（START）**开始，到**停止条件（STOP）**结束：

- **START**：SCL 还是高的时候，SDA 从高变低。
- **STOP**：SCL 还是高的时候，SDA 从低变高。

这两个信号是"特殊的"，因为正常传数据的时候，SDA 只允许在 SCL 低的时候变。所以 SCL 高期间 SDA 的跳变，就被解读成"开始"或"结束"。

### 11.2.3 一帧数据长什么样

一次完整的 I2C 写操作是这样的（看下面这张图）：

![I2C 一次完整传输的时序：起始、地址+R/W、ACK、数据、停止](images/i2c-timing.svg)

拆开讲：

1. 主机发 START。
2. 主机发一个字节：**高 7 位是从机地址**，**最低位是 R/W**（0 = 写，1 = 读）。
3. 被点名的从机在第 9 个时钟把 SDA 拉低，发一个 **ACK**（Acknowledgment）表示"我在"。
4. 然后主机或从机发数据字节，每发完一个字节，对方回一个 ACK。
5. 最后主机发 STOP，总线空闲。

这就是为什么 SSD1306 的地址会有人说 0x3C、有人说 0x78——**0x3C 是 7 位地址**，拼上 R/W=0（写）之后，一个完整的地址字节就是 `0x3C << 1 = 0x78`。如果你要读，那就是 `0x3C << 1 | 1 = 0x79`。HAL 库的函数参数要求你传的是已经拼好 R/W 的那个字节，所以你在代码里看到的是 0x78。

### 11.2.4 F103 的 I2C 寄存器：不用记，但要知道有什么

HAL 库帮你包了，但出问题的时候你得能往下看一层。F103 的 I2C 外设主要寄存器就这几个：

- **CR1**（Control register 1）：启动外设（PE 位）、发送 START、发送 STOP、应答使能。
- **CR2**：时钟分频、中断使能。
- **DR**（Data register）：你要发/收的数据就写这里/读这里。
- **SR1 / SR2**（Status）：START 是否发出去了、地址是否匹配、TXE/RXNE、AF（应答失败）等状态位。

HAL 的 `HAL_I2C_Mem_Write` 内部其实就是按顺序操作这些寄存器：CR1 发 START → 写 DR 发地址 → 等 SR1 的 ADDR 位 → 写 DR 发寄存器地址 → 写 DR 发数据 → 等 BTF → CR1 发 STOP。你不用手写这个流程，但如果通信卡死，用调试器看 SR1 是卡在哪个位上，比瞎猜快得多。

> F103 的硬件 I2C 有个历史上有名的 bug（硅片问题，不是软件 bug），在某些极端时序下会锁死总线。我自己用 HAL 硬件 I2C 配 OLED、BMP280 没踩过，但你要是遇到莫名其妙卡死，可以切到 GPIO 模拟 I2C（软件 I2C），代码网上一搜一大把。

---

## 11.3 SPI 总线：更快，但是更费线

### 11.3.1 四根线各管什么

SPI 的线比 I2C 多：

![SPI 四线连接：SCK/MOSI/MISO/CS 主从对接](images/spi-wiring.svg)

- **SCK**（Serial Clock）：主机产生时钟，和 UART 一样是同步的。
- **MOSI**（Master Out Slave In）：主机发、从机收。
- **MISO**（Master In Slave Out）：从机发、主机收。
- **CS/SS**（Chip Select）：片选，主机拉低哪根 CS，就选中哪个从机。

因为 MOSI 和 MISO 是分开的，SPI 是**全双工**的——一个时钟沿上主机发一位、从机也发一位，数据同时双向流动。这也是它比 I2C 快的原因之一。

F103 的 SPI1 默认引脚：**PA5=SCK、PA6=MISO、PA7=MOSI**，CS 你自己拿个 GPIO 当片选就行（比如 PA4）。

### 11.3.2 CPOL 和 CPHA：四种模式，配错就乱码

SPI 没有统一的"起始/停止"信号，靠 CS 拉低开始、CS 拉高结束。但时钟的**极性**和**采样相位**是可以配的：

- **CPOL**（Clock Polarity）：SCK 空闲时是高还是低。0 = 空闲低，1 = 空闲高。
- **CPHA**（Clock Phase）：第一个时钟沿采样，还是第二个。0 = 第一沿采样，1 = 第二沿采样。

两个一组合就是四种模式（Mode 0 ~ 3）：

![SPI Mode 0 时序：SCK 空闲低，第一个沿采样](images/spi-timing.svg)

绝大多数 OLED、Flash、SD 卡用 **Mode 0**（CPOL=0, CPHA=0），但也有一些设备用 Mode 3。CubeMX 里你选 `Mode = Transmit Only Master`，它默认就是 Mode 0；如果你的 OLED 接 SPI 之后全是乱码或者花屏，先把 Mode 切到 Mode 3 试试，十有八九是这个问题。

### 11.3.3 HAL_SPI_Transmit 内部干了什么

```c
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout);
```

用起来很简单：给它 SPI 句柄、数据指针、长度，它就把数据全发出去。内部大概是：

1. 循环 Size 次：
2. 等 TXE 位（发送空寄存器空了）。
3. 把一个字节写进 DR。
4. 等 BSY 清零。

因为 SPI 是全双工，发的时候 MISO 上也会有数据进来——你如果不需要，忽略就行。要收数据就用 `HAL_SPI_Receive` 或者 `HAL_SPI_TransmitReceive`（同时收发）。

---

## 11.4 实战：点亮 SSD1306 OLED

![SSD1306 0.96 寸 I2C OLED 模块实物图](images/oled-ssd1306.jpg)

我用的是最常见的那款 0.96 寸 128×64 的 OLED 模块，蓝色光，SSD1306 驱动芯片，I2C 接口。淘宝 5 块钱包邮那种。

### 11.4.1 接线

![OLED I2C 接线：VCC→3V3、GND→GND、SCL→PB6、SDA→PB7](images/oled-wiring.svg)

| OLED 引脚 | STM32 (Blue Pill) | 说明 |
|---|---|---|
| GND | GND | 地 |
| VCC | 3V3 | 别接 5V，SSD1306 是 3.3V 器件 |
| SCL | **PB6** | I2C1_SCL |
| SDA | **PB7** | I2C1_SDA |

就四根线，比 LED 还简单。

### 11.4.2 CubeMX 配置

1. 左边 `Connectivity → I2C1` 选 `I2C`。
2. 参数保持默认：I2C Speed Mode = Standard Mode，Clock Speed = 100000 Hz（100k）。够用了，OLED 不需要 400k。
3. 你会看到 PB6 自动变成 I2C1_SCL、PB7 自动变成 I2C1_SDA。
4. Project Manager 里生成代码。

生成之后你会在 `main.c` 里看到 `MX_I2C1_Init()`，句柄叫 `hi2c1`。

### 11.4.3 先扫一下总线：I2C Scanner

在接 OLED 之前，我强烈建议你先跑一个 I2C 扫描程序。因为新手最常踩的坑就是"地址到底是多少"——不同模块的 ADDR 引脚接的高低不一样，你光看手册是猜不出来的。

```c
void I2C_Scan(void) {
    printf("I2C Scanner:\r\n");
    for (uint8_t addr = 1; addr < 128; addr++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, addr << 1, 1, 2) == HAL_OK) {
            printf("Found device at 0x%02X (7-bit) / 0x%02X (8-bit write)\r\n",
                   addr, addr << 1);
        }
    }
    printf("Scan done.\r\n");
}
```

`HAL_I2C_IsDeviceReady` 这个函数本质上就是发一个 START + 地址 + 等 ACK。如果从机回了 ACK，说明它在线。把这个函数在 `main` 初始化之后调一次，打开串口你就能看到总线上所有设备的地址。

我自己那次踩坑就是：OLED 模块 ADDR 悬空时地址是 0x3C，但我焊了个跳线把 ADDR 接了 VCC，地址变成 0x3D，对应的写地址是 0x7A。驱动里还是 0x78，怎么都不通。扫了一下发现是 0x7A，改一行代码就亮了。

### 11.4.4 驱动怎么来

SSD1306 本身没有字库，你要显示什么字、什么图形，全靠 MCU 算好"哪些像素该亮"，然后通过 I2C 一坨一坨刷进去。自己从头写字模取模工具太费劲，我直接拿了 GitHub 上现成的轻量驱动（比如 afiskon 的 ssd1306 库，或者 STM32 社区常用的 syuez 版本），把 `ssd1306.c` 和 `ssd1306.h` 拷进工程，改一行 I2C 句柄指向 `&hi2c1` 就能用。

> 我第一次接 OLED 的时候，在地址上踩了坑。驱动里默认写的是 0x78，我怎么调都不亮，拿逻辑分析仪一看，SDA 根本没动。后来发现是模块的 ADDR 引脚接了高电平，地址变成了 0x7A（也就是 0x3D << 1）。所以如果你 OLED 不出字，先用 I2C 扫描器（I2C scanner）扫一下总线上到底有几个设备、地址是多少，再往驱动里填。

### 11.4.5 应用代码

主函数初始化之后，写几行：

```c
/* USER CODE BEGIN Includes */
#include "ssd1306.h"
#include "font.h"
/* USER CODE END Includes */

/* USER CODE BEGIN 2 */
ssd1306_Init();                              // 发初始化命令序列
ssd1306_Fill(Black);                         // 整个显存缓冲清 0

ssd1306_SetCursor(0, 0);                     // 光标移到 (0, 0)
ssd1306_WriteString("Hello STM32!", Font_7x10, White);

ssd1306_SetCursor(0, 20);
ssd1306_WriteString("I2C SSD1306 ok", Font_7x10, White);

ssd1306_UpdateScreen();                      // 把整个显存刷到屏幕
/* USER CODE END 2 */
```

完整代码在 [code/ch11_oled_i2c.c](code/ch11_oled_i2c.c)。

烧进去，OLED 亮起来，第一行 "Hello STM32!"，第二行 "I2C SSD1306 ok"。我第一次看到这个的时候真的挺开心的——比 LED 有成就感多了。

![OLED 显示中文和图形的效果（实拍参考）](images/oled-chinese-display.jpg)

### 11.4.6 为什么要分"写缓冲"和"刷屏幕"两层

SSD1306 内部显存是 128×64 位 = 1024 字节，但 I2C 一次只能写一小段。如果你想画一个矩形，逐像素地通过 I2C 发命令，每一帧要来回几十次 I2C 通信，慢死。

所以所有成熟的 SSD1306 驱动都这么设计：

1. 在 MCU 的 RAM 里开一个 1024 字节的数组 `buffer[1024]`，叫"显存缓冲"。
2. `ssd1306_SetCursor`、`ssd1306_WriteString`、`ssd1306_DrawPixel` 这些函数，**只改这个数组**，不碰 I2C。
3. 最后调一次 `ssd1306_UpdateScreen()`，把整个 1024 字节一次性通过 I2C 连续写进 SSD1306。

为什么这样快？因为 I2C 通信的"启动条件 + 地址 + 停止条件"是有开销的。如果你 1024 个字节分 1024 次发，每次都要 START、地址、STOP，光开销就占了大头。一次连续写 1024 字节，START 和 STOP 只来一次，效率高得多。

> 所以你在代码里看到：画完所有东西之后，必须调一次 `UpdateScreen()`，否则屏幕什么都不会变。我见过不少人忘了这一句，对着不亮的屏幕调了俩小时初始化。

### 11.4.7 SSD1306 的显存布局：8 页 × 128 列

这个细节很多人不看，但你要画图形的时候会用到。SSD1306 的 128×64 像素不是按行排的，而是分成 **8 个页（page）**，每页 8 行：

- Page 0 = 第 0~7 行
- Page 1 = 第 8~15 行
- ...
- Page 7 = 第 56~63 行

每个页里，一列是 8 个像素（正好一个字节），bit0 是最上面那个像素，bit7 是最下面那个。所以 `buffer[page * 128 + column]` 这个字节的每一位，就代表这一列 8 个像素的亮灭。

你在驱动里看到的 `ssd1306_DrawPixel(x, y)` 大概就是：

```c
void ssd1306_DrawPixel(uint8_t x, uint8_t y, uint8_t color) {
    buffer[x + (y / 8) * 128] |= (1 << (y % 8));   // 算页和列，置位
}
```

知道这个你以后想自己画进度条、画波形就有依据了。

### 11.4.8 底层 I2C 写命令到底长什么样

驱动内部往 OLED 发东西，分两种：命令和数据。通过 I2C 发一个"控制字节"来区分：

- 控制字节 `0x00`：后面跟的是命令（比如设对比度、设显示方向）。
- 控制字节 `0x40`：后面跟的是显存数据（直接刷像素）。

所以底层函数长这样：

```c
void ssd1306_WriteCommand(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    HAL_I2C_Master_Transmit(&hi2c1, 0x78, buf, 2, 100);
}

void ssd1306_WriteData(uint8_t data) {
    uint8_t buf[2] = {0x40, data};
    HAL_I2C_Master_Transmit(&hi2c1, 0x78, buf, 2, 100);
}
```

`HAL_I2C_Mem_Write` 是更常用的版本，它把"控制字节 + 寄存器地址 + 数据"打包发：

```c
// 把 1024 字节显存刷进 SSD1306 的 GDDRAM
HAL_I2C_Mem_Write(&hi2c1, 0x78, 0x40, 1, buffer, 1024, 1000);
//                地址  控制字节  寄存器长度  数据  长度  超时
```

你看，`0x40` 就是那个控制字节，告诉 SSD1306"接下来这一坨都是显存数据"。

### 11.4.9 HAL I2C 的三种工作模式

你可能已经注意到 HAL 里 I2C 的函数名字有好几套：`HAL_I2C_Master_Transmit`、`HAL_I2C_Master_Transmit_IT`、`HAL_I2C_Master_Transmit_DMA`。它们的区别就是"CPU 怎么等数据发完"：

- **阻塞模式（不带 _IT / _DMA）**：CPU 就在那里死等，每发一个字节轮询一次 SR1 寄存器。简单，但发 1024 字节 OLED 显存的时候 CPU 被占住了 80ms，什么别的事都干不了。
- **中断模式（_IT）**：发完一个字节硬件自动中断，中断服务函数里发下一个字节。CPU 可以在两次中断之间干别的事。
- **DMA 模式（_DMA）**：DMA 控制器自动搬数据，CPU 完全不参与，发完 1024 字节只在结束时中断一次。

刷 OLED 这种大块数据，我强烈建议你用 DMA 版本。因为 1024 字节在 100k I2C 上要花 80ms，阻塞模式会让系统卡死 80ms，LED 闪烁都会顿一下；用 DMA 的话，你只要：

```c
HAL_I2C_Master_Transmit_DMA(&hi2c1, 0x78, buffer, 1024);
```

发完就回来干别的，DMA 发完了会调 `HAL_I2C_MasterTxCpltCallback` 通知你。这和[第 10 章 DMA](ch10_DMA.md) 讲的是同一个道理。

### 11.4.10 显示中文怎么搞

上面的代码显示的是英文 ASCII。要显示中文（比如"温度 26.5°C"），你不能直接发中文字符串——SSD1306 本身没有中文字库。思路是：

1. 用取模软件（比如 PCtoLCD2002、或在线取模网站）把你要显示的每个汉字转成 16×16 或 12×12 的点阵数组。
2. 把这些数组存在 Flash 里。
3. 自己写一个 `oled_show_chinese(x, y, "温度")` 函数，逐字查点阵表，画到显存 buffer 里。

比如"温"字 16×16 的点阵数据长这样（16 列 × 16 行 / 8 = 32 字节）：

```c
const uint8_t hanzi_wen[32] = { /* ... 取模软件生成 ... */ };
```

我知道这听起来很麻烦，但你做一次之后就明白了——OLED 上所有"字"本质上都是一张一张的小点阵图，没有任何魔法。如果你嫌麻烦，GitHub 上有人封装好了中文字库（比如 `oledfont.h`），拷进来直接用就行。

### 11.4.11 显示图形：画进度条、画波形

理解了显存是 1024 字节的数组，画图形就是操作这个数组：

```c
// 画一条横线
void draw_hline(uint8_t x, uint8_t y, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        buffer[(y / 8) * 128 + x + i] |= (1 << (y % 8));
    }
}

// 画一个进度条：0~100
void draw_progressbar(uint8_t percent) {
    uint8_t width = percent * 120 / 100;   // 满格 120 像素
    for (uint8_t x = 0; x < width; x++) {
        for (uint8_t y = 30; y < 40; y++) {
            buffer[(y / 8) * 128 + 10 + x] |= (1 << (y % 8));
        }
    }
}
```

做"环境监测仪"的时候，温湿度、气压、电池电量，全用这种小图形展示，比纯文字好看多了。

### 11.4.12 初始化序列到底在干什么

很多人拿到 SSD1306 驱动，看到那一坨 `0xAE, 0xD5, 0xA8, 0xD3...` 的初始化命令，直接照抄但不理解。我把最关键的几条拆开讲，你以后换屏幕或者自己写驱动心里有数：

- `0xAE`：关闭显示（初始化期间先关显示，避免乱闪）。
- `0xD5 0x80`：设置时钟分频，默认 0x80。
- `0xA8 0x3F`：设置多路复用率（MUX ratio），0x3F = 63，对应 64 行。
- `0xD3 0x00`：显示偏移，0 表示无偏移。
- `0x8D 0x14`：**开电荷泵**。SSD1306 面板本身需要 7~9V 电压来点亮 OLED，但模块只有 3.3V 供电，靠芯片内部的电荷泵升压。这一条不开，屏幕永远不亮——这是新手最常漏的。
- `0x20 0x00`：内存寻址模式，0x00 = 水平寻址（我们上面讲的"8 页 × 128 列"那种连续刷）。
- `0xA1` / `0xC8`：屏幕左右翻转、上下翻转，如果你接上之后字是倒的，改这两条。
- `0xAF`：打开显示。

你看，初始化序列其实不是"魔法"，就是把面板的工作模式配成你想要的样子。我有一次拿到一块新模块，字是上下颠倒的，就是把 `0xC8` 改成 `0xC0` 解决的。

---

## 11.5 接 I2C 传感器：举一反三

OLED 点亮之后，你会发现"接 I2C 设备"这件事突然就通了。不管是温湿度、气压、加速度计，套路一模一样：

1. 查数据手册，找 7 位地址和寄存器映射。
2. CubeMX 开 I2C（同一组 SCL/SDA 直接并联，因为 I2C 支持多设备）。
3. 用 `HAL_I2C_Mem_Read` / `HAL_I2C_Mem_Write` 读寄存器。
4. 把原始数据换算成物理量。

比如 BMP280 气压计，地址 0x76（SDO 接 GND）或 0x77（SDO 接 VCC）。读芯片 ID 寄存器（0xD0）应该返回 0x58：

```c
uint8_t id;
HAL_I2C_Mem_Read(&hi2c1, 0x76 << 1, 0xD0, 1, &id, 1, 100);
// id == 0x58 说明芯片在线
```

![BMP280 气压传感器模块（紫色小板）](images/bmp280-sensor-module.jpg)

再比如 AHT20 温湿度传感器，地址固定 0x38，开机后要发一条初始化命令（0xBE）才能测温度湿度：

![AHT20 温湿度传感器模块（绿色小板，VIN/GND/SCL/SDA）](images/aht20-sensor-module.jpg)

```c
// 触发测量
uint8_t trigger[3] = {0xAC, 0x33, 0x00};
HAL_I2C_Mem_Write(&hi2c1, 0x38 << 1, 0x00, 1, trigger, 3, 100);
// 等 80ms
HAL_Delay(80);
// 读 7 个字节：状态 + 湿度 20bit + 温度 20bit + CRC
uint8_t raw[7];
HAL_I2C_Mem_Read(&hi2c1, 0x38 << 1, 0x00, 1, raw, 7, 100);
```

读出来的是原始二进制，换算公式手册里都有，照着抄就行。

### 11.5.1 多个 I2C 设备挂在同一组线上怎么办

这是 I2C 的优势——同一组 SCL/SDA 上可以挂很多设备。但前提是**地址不能撞**：

![I2C 多设备地址冲突示意：两块 0x3C 的 OLED 会互相踩](images/i2c-address-conflict.svg)

比如你同时挂一块 OLED（0x3C）和一个 BMP280（0x76），没事，它们地址不同，主机发 0x3C 只有 OLED 应答，发 0x76 只有 BMP280 应答。

但如果你想挂两块一样的 OLED 显示不同内容，问题就来了——两块都是 0x3C，主机一发地址，两块都应答，数据互相踩。解法有两种：

1. 买地址可配的 OLED（ADDR 引脚拉高就变 0x3D），一块悬空一块拉高。
2. 用 I2C 多路复用芯片 TCA9548A，它能把一组 I2C 切成 8 组，每组独立寻址。

---

## 11.6 I2C 还是 SPI：OLED 怎么选

你去淘宝搜 OLED，会看到 I2C 版（4 脚）和 SPI 版（7 脚）两种。我给你我的经验：

| 维度 | I2C OLED (4脚) | SPI OLED (7脚) |
|---|---|---|
| 接线 | 4 根 | 7 根（多 RES/DC/CS） |
| 刷新速度 | 1024 字节 @ 100k ≈ 80ms/帧 | 1024 字节 @ 10M ≈ 1ms/帧 |
| 适合场景 | 文字、简单 UI、仪表盘 | 动画、波形刷新、TFT 联动 |
| 引脚占用 | 2 个 | 4 个（SCK/MOSI + 3 GPIO） |
| 代码改动 | 现成 | 底层写函数从 I2C 换成 SPI |

我的建议：**新手先用 I2C 版**，接线少、调通了有信心。等你要做动画（比如心率条、波形滚动），再换 SPI 版——上层 `ssd1306_WriteString` 这些函数一行不用改，只换底层 `ssd1306_WriteCommand/Data` 的实现，从 `HAL_I2C_Master_Transmit` 换成 `HAL_SPI_Transmit` + 拉 DC/CS 就行。

SPI 版 OLED 接线表（参考）：

| OLED (SPI) | STM32 |
|---|---|
| GND / VCC | GND / 3V3 |
| SCK (D0) | PA5 (SPI1_SCK) |
| SDA (D1/MOSI) | PA7 (SPI1_MOSI) |
| RES | 任意 GPIO（PA3 例） |
| DC | 任意 GPIO（PA2 例） |
| CS | 任意 GPIO（PA4 例） |

DC 引脚是"命令/数据"选择器：DC 拉低 = 后面发的是命令，拉高 = 后面发的是显存数据。比 I2C 的控制字节更直接。

---

## 11.7 踩过的坑，给你提个醒

我自己在 I2C/SPI 这条路上踩过的坑，整理一下：

**1. OLED 完全不亮，没反应。**
先查四件事：VCC 是不是接了 3.3V（接 5V 有些模块会烧，有些不亮）；SCL/SDA 是不是接反了（PB6/PB7 别搞混）；地址是不是对（0x78 还是 0x7A，跑个 I2C scanner）；是不是忘了调 `ssd1306_UpdateScreen()`。

**2. OLED 亮了但全是花点/乱码。**
初始化序列不对。不同厂家的 SSD1306 模块初始化命令略有差异（尤其是对比度、电荷泵），换个完整的初始化序列试试。或者显存缓冲没清，上电时 buffer 是随机值。

**3. I2C 通信卡死，HAL 返回 HAL_I2C_ERROR_BUSY。**
通常是 SDA 被从机拉低了（上一次通信被从机打断，从机一直占着线）。解法：软件上手动发 9 个 SCL 时钟把总线释放；或者重启 MCU。硬件上：检查上拉电阻阻值是不是太大（线长超过 20cm 时 4.7k 可能不够）。

**4. SPI 接 TFT 彩屏全是乱码。**
先切 Mode 0 ↔ Mode 3，八成是 CPOL/CPHA 不匹配。再查 DC 引脚的电平时机对不对。

**5. 多个 I2C 设备只能用一个。**
跑 I2C scanner 看地址。如果两个设备地址一样（比如两块 0x3C OLED），按上面讲的改跳线或者上 TCA9548A。

**6. 5V 系统接 3.3V 模块。**
F103C8T6 是 3.3V 器件，但有些教程板用 5V 给 VCC。I2C/SPI 模块必须 3.3V，否则即使不烧也会工作不正常。

**7. 线太长导致通信不稳定。**
面包板上杜邦线接得乱七八糟，SDA/SCL 拉到 30cm 以上，100k 还能跑，400k 就出错。这是因为总线寄生电容大了，上拉电阻要换小一点（2.2k），或者降速到 100k。做项目的时候线尽量短。

---

## 11.8 动手做：环境监测仪的雏形

把这一章和前面学的东西拼起来，你已经能做一个"环境监测仪"了：

- AHT20 读温湿度（I2C，地址 0x38）。
- BMP280 读气压（I2C，地址 0x76，和 AHT20 共用 PB6/PB7）。
- OLED 显示温度、湿度、气压（I2C，地址 0x78，同一组总线）。
- 串口每隔 2 秒打印一次数值（DMA 发送，[第 8 章串口](ch08_串口.md) + [第 10 章 DMA](ch10_DMA.md)）。

主循环长这样：

```c
while (1) {
    aht20_read(&temp, &humi);
    bmp280_read(&pressure);

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    char buf[24];
    sprintf(buf, "T:%.1fC H:%.0f%%", temp, humi);
    ssd1306_WriteString(buf, Font_7x10, White);
    sprintf(buf, "P:%.0f Pa", pressure);
    ssd1306_SetCursor(0, 20);
    ssd1306_WriteString(buf, Font_7x10, White);
    ssd1306_UpdateScreen();

    printf("T=%.2f H=%.2f P=%.0f\r\n", temp, humi, pressure);
    HAL_Delay(2000);
}
```

这个小东西花不了三十块钱，但能让你把 I2C、OLED、串口、ADC 串起来跑一遍，比光看教程强十倍。

---

## 11.9 本章小结

- I2C 是两线半双工总线，开漏 + 上拉，靠 7 位地址区分从机，速度 100k/400k，适合接传感器、OLED 这类慢速设备。
- SPI 是四线全双工总线，靠片选区分从机，速度几十 MHz，适合 Flash、TFT 屏这类高速设备。
- SSD1306 OLED 的驱动分三层：底层 HAL 写命令/数据、中层显存缓冲、上层图形 API；最后一定要 `UpdateScreen()` 才会刷新。
- 接任何 I2C 传感器（AHT20、BMP280、MPU6050）都是同一套路：地址 + 寄存器映射 + `HAL_I2C_Mem_Read/Write`。
- I2C 多设备共享总线的前提是地址不冲突。

**自测题：**

1. I2C 的 SCL/SDA 为什么要开漏 + 上拉？直接推挽输出不行吗？
2. SSD1306 的 0x3C 和 0x78 是什么关系？读操作地址又是多少？
3. 为什么 OLED 驱动要在 MCU RAM 里开一个 1024 字节的 buffer，而不是直接写显存？
4. SPI 的 CPOL=1, CPHA=1 是 Mode 几？这种模式下 SCK 空闲是高还是低？
5. 你现在要同时挂两块 OLED 和一个 BMP280 到 STM32 的同一组 I2C 上，会遇到什么问题？怎么解决？

下一章我们会离开"硬件外设"，进入一个完全不同的维度——[第 12 章 FreeRTOS](ch12_FreeRTOS.md)，学会怎么让 STM32 "同时干多件事"。

---

<!-- ===== 第11章 完 ===== -->
