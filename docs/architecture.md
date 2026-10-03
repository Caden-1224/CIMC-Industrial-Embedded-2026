# 系统架构与任务调度

本文档描述 GD32F470 工业多通道数据采集与远程升级终端的软件架构、任务调度模型、初始化流程、数据通路与状态维护方式。文中所有结论均来自当前工程源码；无法从源码确认的内容显式标注为「待核实」。OTA 升级的状态机、分区细节与掉电边界由另一份文档负责，本文只给出分区总览与工程定位。

> 系统架构与数据链路总览图见 [assets/architecture-zh-light.svg](../assets/architecture-zh-light.svg)（深色主题：[architecture-zh-dark.svg](../assets/architecture-zh-dark.svg)；英文版为 `architecture-en-*.svg`）。

## 1. 项目定位与赛题背景

本工程是 2026 年 CIMC「西门子杯」中国智能制造挑战赛工业嵌入式系统开发方向的参赛作品，实现一个面向工业现场的数据采集与远程升级终端：采集多路模拟量，通过 RS485 以自定义 ASCII-Hex 帧协议与上位机交互，支持参数掉电保存、定时自动上报、越限告警记录、RTC 定时唤醒低功耗，并支持经 BootLoader 的固件在线升级。

软件由 `CIMC_App` 与 `CIMC_BootLoader` 两套独立的 Keil MDK 工程组成，不使用 RTOS，主循环采用固定数组顺序的协作式时间片轮询。驱动程序基于 GigaDevice 官方 GD32F4xx 标准外设库，业务逻辑、协议编解码与升级流程为自研实现。

## 2. 系统组成

### 2.1 MCU

两套工程的目标器件均为 `GD32F470VE`（GigaDevice，Keil Pack `GigaDevice.GD32F4xx_DFP.3.0.3`），内核为 Cortex-M4（含 FPU）。

| 项目 | 取值 | 来源 |
|---|---|---|
| 目标器件 | `GD32F470VE` | `CIMC_App/project/CIMC_App.uvprojx`（`<Device>`）、`CIMC_BootLoader/project/CIMC_BootLoader.uvprojx` |
| 预定义宏 | `USE_STDPERIPH_DRIVER,GD32F470` | 同上（`<Define>`） |
| 内部 Flash | 512 KiB（`BL_FLASH_TOTAL_SIZE = 0x00080000`） | `CIMC_App/common/bl_partition.h:7`；工程 Flash 算法为 `GD32F4xx_512KB` |
| 主 SRAM / SRAM2 | `0x20000000` 长 `0x030000`、`0x10000000` 长 `0x010000` | 两个 `.uvprojx` 的 `<Cpu>` 行 |
| App 链接区间 | `0x08011000` 长 `0x020000`（128 KiB） | `CIMC_App/project/CIMC_App.uvprojx`（`IROM`）、`CIMC_App/common/bl_partition.h:18` |
| BootLoader 链接区间 | `0x08000000` 长 `0x010000`（64 KiB） | `CIMC_BootLoader/project/CIMC_BootLoader.uvprojx`（`IROM`）、`CIMC_App/common/bl_partition.h:12` |
| 系统时钟 | 240 MHz（PLL 基于 25 MHz 晶振，`__SYSTEM_CLOCK_240M_PLL_25M_HXTAL`） | `CIMC_App/CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c:65` |

说明：`GD32F470VE` 是源码与工程文件中可见的确定信息；若后续更换同系列其它容量型号，需同步核对 `bl_partition.h` 中的容量常量与两套工程的 IROM 配置。

### 2.2 板卡构成

系统的物理构成分三层：

1. **主控开发板**：`GD32F470VE` 开发板（`CIMC_GD32F470 Development Kit V2.0`），提供 MCU、板载电位器、DAC 输出脚、OLED、板载 SPI Flash、RS485/RS232 收发链路、RTC 晶振与按键 LED 资源。具体引脚分配见 [`docs/hardware-pinout.md`](hardware-pinout.md)。
2. **自研电源板**：18–36 V 工业输入经防反接与 DC-DC 降压到 5 V，为开发板与外接模拟板供电。软件侧不感知该板，仅通过系统整体供电稳定性间接相关。
3. **自研 PT100 采样板**：恒流激励三线制 PT100，仪表放大器放大毫伏级差分电压，经 RC 滤波后送入外部 ADC（GD30AD3344）。软件侧对应 CH2 采样链路。
4. **自研精密电阻模拟测试板**：以 0.1% 精密电阻与跳线挡位模拟 PT100 在不同温度点的阻值，用 I+/I−/COMP 三线接口接入采样板，用于在没有标准温度源的条件下验证 CH2 采集链路。

硬件板卡的器件级细节、PCB 布局与精度指标不在本文范围内；相关的原理图复核与实测数据仍待补齐。

### 2.3 外部芯片

| 芯片 | 接口 | 在软件中的位置 |
|---|---|---|
| GD30AD3344（外部 16 位 ΔΣ ADC） | SPI3（`PE12` SCK / `PE13` MISO / `PE14` MOSI），`PE10` 片选，DMA1 CH1/CH0 搬运 | `CIMC_App/Driver/GD30AD3344/`、`CIMC_App/Function/Gd30ad3344_service.c` |
| OLED（128×32，SSD1306 类） | 软件模拟 I2C（`PB8` SCL / `PB9` SDA，从机地址 `0x78`） | `CIMC_App/Driver/OLED_IIC/`、`CIMC_BootLoader/Driver/OLED_IIC/bl_oled.c` |
| 板载 SPI Flash | SPI0（`PA15` 片选 / `PB3` SCK / `PB4` MISO / `PB5` MOSI） | `CIMC_App/Driver/SPI_FLASH/`、`CIMC_App/Function/Spi_flash_service.c` |
| RS485 收发器 | USART1（`PD5` TX / `PD6` RX），`PE8` 控制 DE/RE | `CIMC_App/Driver/RS485/` |

## 3. 软件分层架构

### 3.1 分层图

```text
+---------------------------------------------------------------+
| User 层            main.c / systick.c / gd32f4xx_it.c          |
|  main() -> System_Init() -> UsrFunction() -> System_Loop()     |
|  1 ms 时基（SysTick）+ 中断向量实现                             |
+---------------------------------------------------------------+
| Function 层        任务表与业务服务                             |
|  Function.c        poll_tasks[] / System_Init / System_Loop    |
|  *_service.c       Adc / Dac / Gd30ad3344 / Rs485 / AutoReport |
|                    Alert / Sleep / Oled / Led / Key / Rtc      |
|                    Spi_flash / Usart / App_param / BootUpdate  |
+---------------------------------------------------------------+
| Protocol 层        帧编解码与校验，仅依赖标准库                 |
|  cimc_protocol.c   ASCII-Hex 编解码 / 帧解析组帧 / CRC16 / CRC32|
|                    / 大端 16/32 位与 IEEE754 float 读写         |
|  ota_protocol.c    OTA 流式包头校验（仅 App 侧）                |
+---------------------------------------------------------------+
| Driver 层          寄存器级外设驱动                             |
|  ADC / DAC / GD30AD3344 / RS485(+DMA/IDLE) / USART(+DMA/IDLE)  |
|  OLED_IIC / SPI_FLASH / InternalFlash / RTC / TIMER_BASIC      |
|  TIMER_PWM / LED / KEY / BootConfig                            |
+---------------------------------------------------------------+
| common 层          跨工程共享定义                               |
|  bl_partition.h    Flash 分区常量与编译期一致性检查             |
|  bl_param.h        bl_param_t / bl_log_entry_t 与状态标志       |
|  ota_package_config.h（App 侧）OTA 包头常量                     |
+---------------------------------------------------------------+
| 硬件抽象            CMSIS core_cm4.h / 启动文件 / GD32F4xx 标准外设库 |
+---------------------------------------------------------------+
```

### 3.2 各层职责

- **Driver 层**：只负责寄存器级配置、数据搬运与外设状态查询，不做业务换算、不做协议解析。典型接口如 `ADC_ReadChannelRaw()`（`CIMC_App/Driver/ADC/ADC.h`）、`RS485_SendBytes()`（`CIMC_App/Driver/RS485/RS485.h`）、`InternalFlash_WriteBytes()`（`CIMC_App/Driver/InternalFlash/InternalFlash.h`）。
- **Protocol 层**：只处理「字节 ↔ 结构」的转换与校验，不访问任何外设，也不包含业务语义。`cimc_protocol.c` 提供 ASCII-Hex 编解码、帧解析/组帧、CRC16-Modbus、反射 CRC32、大端字段与 `float` 大端编码（`CIMC_App/Protocol/cimc_protocol.h`）。
- **Function 层**：业务服务层。每个服务 = 一个 `Xxx_Service_Init()` + 一个 `Xxx_Service_Task()`（可被 `poll_tasks` 调用），服务之间只通过共享结果结构与显式 getter 交互，不直接操作寄存器。
- **common 层**：App 与 BootLoader 共同包含的头文件目录，用于保证两边对 Flash 分区、参数块布局、升级状态标志的理解完全一致。两个工程各自持有一份同名同内容的副本（`CIMC_App/common/`、`CIMC_BootLoader/common/`），需人工保持同步。
- **User 层**：`main()` 只做三件事——`System_Init()`、`UsrFunction()`，后者内部是 `while(1) { System_Loop(); }`（`CIMC_App/User/main.c`、`CIMC_App/Function/Function.c:83`）。`systick.c` 提供 1 ms 时基与 `get_millis()`；`gd32f4xx_it.c` 只做向量转发。

### 3.3 App 与 BootLoader 的共享方式

两个工程共用同一套 `Protocol/` 与 `common/` 源码布局，但各自维护物理副本：

- `CIMC_App/Protocol/cimc_protocol.c/.h` 与 `CIMC_BootLoader/Protocol/cimc_protocol.c/.h`：内容一致，BootLoader 侧用其中的 `CimcProtocol_Crc32Calc()` 校验下载区镜像。
- `CIMC_App/common/bl_param.h` 与 `CIMC_BootLoader/common/bl_param.h`：定义同一份 `bl_param_t` 参数块与 `BL_UPDATE_FLAG_*` 状态字。跨复位通信完全依赖这一份位于 `0x08010000` 参数页的结构体，两边的字段布局必须逐字节一致，否则 BootLoader 会因 CRC 校验失败而回退默认参数。
- `ota_protocol.c` 只存在于 App 侧，用于校验 USART0 流式升级包头（`CIMC_App/Protocol/ota_protocol.c`）。
- BootLoader 侧不包含任何 `Function/*_service.c`，其业务入口是单一函数 `bootloader_run()`（`CIMC_BootLoader/Function/bl_core.c:1227`），由 `UsrFunction()` 调用一次后进入空循环（`CIMC_BootLoader/Function/Function.c:14`）。

## 4. 任务调度

### 4.1 任务表

调度器定义在 `CIMC_App/Function/Function.c`：

```c
typedef struct {
    void (*task_func)(void);
    uint32_t period_ms;
    uint32_t last_ms;
} slice_poll_t;                                    // Function.c:3-8
```

`poll_tasks` 完整表（`CIMC_App/Function/Function.c:10-23`，按数组顺序执行）：

| 序号 | 任务函数 | 周期 ms | 源码行 | 主要职责 |
|---|---|---|---|---|
| 0 | `Led_Service_Task` | 10 | `Function.c:11` | 1 s 心跳翻转 LED2，把 `led_state[]` 映射到 GPIO |
| 1 | `Key_Service_Task` | 20 | `Function.c:12` | 扫描 6 路按键，切换 PWM 占空比与 LED 状态 |
| 2 | `Usart_Service_Task` | 10 | `Function.c:13` | 取 USART0 帧，优先交给 OTA 升级处理，其次 `time` 调试命令 |
| 3 | `BootUpdate_Service_Task` | 20 | `Function.c:14` | 消费 32 KiB 环形缓冲，解析流式升级包头并写下载区 |
| 4 | `Adc_Service_Task` | 50 | `Function.c:15` | 内部 ADC 读 CH0/CH1 原始码，换算 mV 并维护 8 点滑动平均 |
| 5 | `Gd30ad3344_Service_Task` | 50 | `Function.c:16` | SPI/DMA 读外部 ADC，换算 PT100 电阻值 |
| 6 | `AutoReport_Service_Task` | 20 | `Function.c:17` | 到点时发送 CH0/CH1 自动上报帧 |
| 7 | `Alert_Service_Task` | 200 | `Function.c:18` | 检查 CH0/CH1 越限上升沿，写告警记录 |
| 8 | `Sleep_Service_Task` | 10 | `Function.c:19` | 处理睡眠请求，配置 RTC ALARM0 后进入 deep sleep |
| 9 | `Oled_Service_Task` | 50 | `Function.c:20` | 刷新 OLED 两行状态文本 |
| 10 | `RTC_Service_Task` | 1000 | `Function.c:21` | 比较秒值变化，维护 `rtc_healthy` |
| 11 | `Rs485_Service_Task` | 10 | `Function.c:22` | 取 RS485 帧，协议解析、命令分发、待复位/心跳处理 |

任务数量由 `sizeof` 在编译期推导：`TASK_COUNT = sizeof(poll_tasks)/sizeof(poll_tasks[0])`（`Function.c:25`），增删任务只需改数组。

### 4.2 调度算法语义

```c
void System_Loop(void)                              // Function.c:68
{
    uint32_t current_ms = get_millis();             // 整个循环只取一次时基
    for (task_index = 0U; task_index < TASK_COUNT; task_index++) {
        if ((current_ms - poll_tasks[task_index].last_ms) >= poll_tasks[task_index].period_ms) {
            poll_tasks[task_index].task_func();
            poll_tasks[task_index].last_ms = current_ms;
        }
    }
}
```

要点：

1. **到期判据是无符号差值比较**：`(current_ms - last_ms) >= period_ms`。因为两个操作数都是 `uint32_t`，减法按模 2^32 进行，得到的是「距上次执行已过去的毫秒数」，与绝对值大小无关（`Function.c:75`）。
2. **`last_ms = current_ms` 而非 `last_ms += period_ms`**：执行完成后把上次时间戳直接设为本次循环的入口时刻，不保留绝对节拍。因此：
   - 一个周期内只执行一次，不会因为迟到而突发补跑多次；
   - 实际节拍相对目标周期会漂移（慢调用后下一次触发点整体后移）；
   - 同一轮循环里排在后面的任务使用的仍是循环入口时刻，不包含前面任务刚刚消耗的时间，即同一轮内的判断基准不对后续任务让步。
3. **没有任务独立栈**：`poll_tasks` 里存的是普通函数指针，任务函数直接在 `main` 的栈上运行，与其余代码共用同一个栈，不切换 MSP/PSP。
4. **不能抢占**：调度器只在主循环中顺次调用函数，没有任何基于中断或优先级的上下文切换。数组顺序就是执行顺序，排在后面的任务无法打断前面的任务。
5. **慢函数会推迟后续任务**：任何一个服务阻塞（等待 DMA 完成、等待 Flash 就绪、等待 1 ms 延时等）都会把同一轮里排在它之后的任务以及下一轮的全部任务一起推迟。实际工程中最明显的阻塞点是 Flash 页擦写与内部 ADC 的顺序等待。
6. **`period_ms` 是调用机会的下限，不是响应时间保证**：表中数值表示「距上次执行至少过去这么久才再次调用」，不构成最坏响应时间承诺。任务的真实响应时间取决于其它任务的执行时长、阻塞时长与中断开销，本项目尚未做实测标定（**待核实**）。

### 4.3 uint32_t 回绕安全性与前提

`get_millis()` 返回累加的 1 ms 计数（`CIMC_App/User/systick.c:89-101`），约 49.7 天后回绕。回绕本身不会导致调度器误判，前提与原因如下：

- **前提一：差值比较而非绝对值比较**。判断写成 `(current_ms - last_ms) >= period_ms`，而不是 `current_ms >= last_ms + period_ms`。前者在模运算下始终得到真实的间隔（只要间隔本身小于 2^32），后者在 `last_ms + period_ms` 溢出时会得到错误结论。
- **前提二：真实间隔远小于半个计数周期**。该写法能正确表示任意不超过 2^32−1 的间隔，但它对「停顿了整整一个计数周期」这种情况无法区分 0 与 2^32，也无法区分「刚过去」与「过去了 49.7 天」。本项目周期最大 1000 ms，只要系统没有连续停机数十天，该前提自然成立。
- **前提三：`last_ms` 与 `current_ms` 必须同源且同为 32 位无符号**。`poll_tasks` 的字段类型为 `uint32_t`，`get_millis()` 返回 `uint32_t`，两侧类型一致才能保证按无符号模运算比较。
- 同样的「半范围差值」技巧在服务内部也被复用：`AutoReport_Service_Task` 用 `(uint32_t)(now_ms - next_ms) < 0x80000000UL` 判断是否到点（`CIMC_App/Function/AutoReport_service.c:93`），`Sleep_Service_Task` 用同一形式判断延时是否耗尽（`CIMC_App/Function/Sleep_service.c:145`），`Rs485_Service_IsDeadlineReached()` 同理（`CIMC_App/Function/Rs485_service.c:33`）。

### 4.4 中断与调度的关系

调度器本身不涉及中断，但中断会与它共享数据：

- `SysTick_Handler()` 只做两件事——`delay_decrement()` 驱动阻塞延时、`tick_increment()` 累加毫秒计数（`CIMC_App/User/gd32f4xx_it.c:87-91`）。GPIO 操作一律放在任务里，不在 ISR 内执行。
- `USART1_IRQHandler()`（RS485 接收）在 IDLE 事件里把 DMA 缓冲搬进帧缓冲并置 `s_frame_ready`（`CIMC_App/Driver/RS485/rs485_dma.c:288`）；`USART0_IRQHandler()`（升级流）在 RBNE/IDLE 事件里把字节写入环形缓冲或拷贝为整帧（`CIMC_App/Driver/USART/usart_dma.c`）。
- `RTC_Alarm_IRQHandler()` 只委托 `Sleep_Service_RtcAlarmIrqHandler()` 清标志并置唤醒标志（`CIMC_App/User/gd32f4xx_it.c:95-98`）。
- 结论：这是「中断收集 + 主循环协作调度」的模型，中断不触发任务切换，只改变共享状态或标志，由任务在下一次被调用时消费。

### 4.5 与模板工程的对照

同一套写法在作者另一份工程里被整理成了可复用模板：[Caden-1224/stm32-polled-scheduler](https://github.com/Caden-1224/stm32-polled-scheduler)（STM32F407 + HAL，调度器在 `APP/polled_scheduler.c`，1181 字节）。两处结构一致，可以对照阅读：

| 对照项 | 本工程 `CIMC_App/Function/Function.c` | 模板 `APP/polled_scheduler.c` |
| --- | --- | --- |
| 任务结构体 | `slice_poll_t { task_func, period_ms, last_ms }` | `polled_task_t { task_func, rate_ms, last_run }` |
| 任务表 | `poll_tasks[]`，12 项，周期 10 / 20 / 50 / 200 / 1000 ms | `polled_task[]`，4 项，另有 `rate_ms = 0` 的「每轮都跑」任务（FOC） |
| 时间源 | `get_millis()`（SysTick 累加），每轮循环只取一次 | `HAL_GetTick()`，每个任务被检查时各取一次 |
| 到期判断 | `(current_ms - last_ms) >= period_ms` | `(now_time - last_run) >= rate_ms` |
| 时间戳更新 | `last_ms = current_ms` | `last_run = now_time` |
| 溢出处理 | 依赖 `uint32_t` 差值（同 4.3 节） | 源码注释同样写明「采用时间差比较，天然兼容 tick 溢出」 |

差异只在实际取舍，不在模式：本工程把 tick 取一次化为「同一次循环内所有任务看到同一个时间」，代价是循环后半段的任务判断使用的是入口时刻；模板给每个任务重新取时间，代价是任务多时 `HAL_GetTick()` 调用次数增加。两处都没有任务栈、没有抢占，也都只保证「不早于周期被检查」。

## 5. 初始化与启动流程

### 5.1 从复位到主循环

1. **上电复位**：内核从 `0x08000000` 取 MSP 与复位向量，进入 BootLoader（BootLoader IROM 起始即 `0x08000000`）。
2. **BootLoader 初始化**：`main()` → `System_Init()`：`systick_config()` → `my_usart_init(bootloader_get_upgrade_baudrate())` → `BlOled_Init()` → `BlOled_ShowBootloader()`（`CIMC_BootLoader/Function/Function.c:6-12`）。升级通道波特率取自共享参数块，非法参数回退默认值（`CIMC_BootLoader/Function/bl_core.c:371`）。
3. **BootLoader 启动决策**：`bootloader_run()` 读取主/备 `bl_param_t`，按 `update_counter` 选择有效副本并修复损坏一侧，再依据 `update_flag` 分流到等待升级或跳转 App（`CIMC_BootLoader/Function/bl_core.c:1227`）。升级状态机的完整细节由 OTA 文档负责，本文不展开。
4. **权限与现场释放**：跳转前先关闭 BootLoader 占用的 USART1 中断、清 NVIC pending、把 RS485 DE 拉低并关断 USART1（`bl_release_uart_before_app_jump()`，`bl_core.c:1161`）。
5. **跳转 App**：`bl_jump_to_app()` 关闭全局中断 → 关停并清零 SysTick → 遍历 8 组 NVIC 清 `ICER/ICPR` → `__DSB()/__ISB()` → `SCB->VTOR = app_base` → `__set_MSP(*(uint32_t*)app_base)` → 取 `app_base + 4` 作为复位入口 → 开中断并调用（`CIMC_BootLoader/Function/bl_core.c:1191-1225`）。App 侧基址为 `BL_APP1_START_ADDR = 0x08011000`（`CIMC_App/common/bl_partition.h:18`）。
6. **App 入口**：`main()` → `System_Init()` → `UsrFunction()`，后者进入 `while(1) { System_Loop(); }`（`CIMC_App/User/main.c`、`CIMC_App/Function/Function.c:83-89`）。

### 5.2 App 的 `System_Init()` 顺序

App 启动时再次设置向量表并重建全部外设，顺序严格照源码（`CIMC_App/Function/Function.c:27-66`）：

| 步骤 | 语句 | 行号 | 说明 |
|---|---|---|---|
| 1 | `__disable_irq()` | 29 | 关全局中断，保证向量表切换原子性 |
| 2 | `SCB->VTOR = BL_APP1_START_ADDR` | 30 | 把异常向量表指向 App 区 |
| 3 | `__DSB(); __ISB()` | 31-32 | 指令同步屏障 |
| 4 | `systick_config()` | 34 | 1 kHz SysTick，优先級 0，建立毫秒时基 |
| 5 | `__enable_irq()` | 35 | 开中断，之后初始化允许延时与中断工作 |
| 6 | `Led_Service_Init()` | 36 | LED GPIO 与状态数组 |
| 7 | `Key_Service_Init()` | 37 | 按键 GPIO |
| 8 | `Usart_Service_Init()` | 38 | USART0 115200 + DMA，打印 Ready |
| 9 | `BootUpdate_Service_Init()` | 39 | 复位 OTA 状态机并初始化 32 KiB 环形缓冲 |
| 10 | `AppParam_Service_Init()` | 40 | 从共享参数页加载业务参数，无效则写默认值 |
| 11 | `Adc_Service_Init()` | 42 | ADC 端口初始化、延时 50 ms、预填两路滑动平均 |
| 12 | `Dac_Service_Init()` | 44 | DAC0 OUT0 上电，并恢复持久化的 DAC 原始码 |
| 13 | `Gd30ad3344_Service_Init()` | 46 | SPI3/DMA/GPIO/片选初始化并立即采样一次 |
| 14 | `AutoReport_Service_Init()` | 48 | 自动上报停止态，采集 LED 熄灭 |
| 15 | `Alert_Service_Init()` | 50 | 从参数页读告警记录，CRC 无效则重建并抓取基线 |
| 16 | `Sleep_Service_Init()` | 52 | 清除睡眠请求与唤醒标志 |
| 17 | `Oled_Service_Init()` | 54 | OLED 初始化并立刻刷一屏状态 |
| 18 | `SpiFlash_Service_Init()` | 56 | 板载 SPI Flash 初始化并读 JEDEC ID |
| 19 | `RTC_Service_Init()` | 58 | RTC 硬件初始化与配置状态判断 |
| 20 | `Rs485_Service_Init()` | 60 | 按持久化波特率初始化 USART1 RS485，并登记启动心跳 |
| 21 | `Timer_Basic_Init()` | 62 | TIM6 基本定时器，1 Hz 更新中断 |
| 22 | `my_timer_init()` | 64 | TIMER2_CH0 / PC6 PWM 输出 |

顺序上有两处隐含依赖：参数服务（步骤 10）必须先于 DAC（步骤 12）、告警（步骤 15）和 RS485（步骤 20），因为后三者都要读取持久化参数；`systick_config()`（步骤 4）必须先于所有使用 `delay_1ms()` 或 `get_millis()` 的服务。初始化完成后，`UsrFunction()` 无限循环调用 `System_Loop()`（`Function.c:83-89`），每轮按 4.1 的表顺序检查 12 个任务是否到期；任务的启动、停止、睡眠请求、待复位等状态都由任务自身读取标志维护，调度器不参与管理。

## 6. 数据通路

三路业务通道的采集来源、换算链与单位如下。三路最终统一到同一个协议出口 `Rs485_Service_GetChannelValue()`（`CIMC_App/Function/Rs485_service.c:278`），使单次查询与自动上报复用同一份换算结果。

### 6.1 CH0：内部 ADC 读板载电位器

| 阶段 | 内容 | 单位 | 源码 |
|---|---|---|---|
| 采样 | `ADC_ReadChannelRaw(ADC_CHANNEL_10)`，对应引脚 `PC0` | 原始码 0..4095 | `Function/Adc_service.c:45`、`Driver/ADC/ADC.c:228` |
| 电压换算 | `raw × 3300 / 4095`，定点整数运算 | mV | `Function/Adc_service.c:61-64` |
| 滤波 | 8 点滑动平均，CH0 独立缓冲与独立累加和 | mV | `Function/Adc_service.c:74-89` |
| 业务换算 | `smooth × CH0 变比`（`AppParam_Service_GetRatio`） | 由变比决定，默认 1.0 时为 mV | `Function/Rs485_service.c:282` |
| 协议编码 | `CimcProtocol_WriteFloatBe()` 以 IEEE754 大端 4 字节输出 | — | `Function/Rs485_service.c:270-276` |

采样时间为 `ADC_SAMPLETIME_144`，每次读取重新配置通道、清 EOC、软件触发、等待 EOC 并带轮询上限（`Driver/ADC/ADC.c:228-247`），即在任务内同步阻塞完成一次转换。

### 6.2 CH1：内部 ADC 读 DAC 输出回读

链路结构与 CH0 完全相同，差别只在通道与物理来源：

| 阶段 | 内容 | 单位 | 源码 |
|---|---|---|---|
| 激励 | DAC0 OUT0（`PA4`）输出 12 位码，硬件上跳接到 `PC1` | 原始码 0..4095 | `Driver/DAC/DAC.c:40-49` |
| 采样 | `ADC_ReadChannelRaw(ADC_CHANNEL_11)`，对应 `PC1` | 原始码 0..4095 | `Function/Adc_service.c:50` |
| 电压换算 | `raw × 3300 / 4095` | mV | `Function/Adc_service.c:61-64` |
| 滤波 | CH1 独立 8 点滑动平均缓冲 | mV | `Function/Adc_service.c:133-136` |
| 业务换算 | `smooth × CH1 变比` | 由变比决定 | `Function/Rs485_service.c:286` |

CH0/CH1 共用同一个滑动平均环形下标 `s_smooth_idx`，因为两路永远在同一次任务调用中一起更新，统一推进下标可保证两路窗口在时间上对齐（`Function/Adc_service.c:138`）。DAC 原始码经 `AppParam_Service_SetDacRaw()` 持久化，重启时由 `Dac_Service_Init()` 恢复（`Function/Dac_service.c:7-18`），否则重启后 CH1 会掉到 0 V 附近。

### 6.3 CH2：GD30AD3344 经 SPI/DMA 读 PT100 调理电压

| 阶段 | 内容 | 单位 | 源码 |
|---|---|---|---|
| 采样 | SPI3 全双工主机，DMA1 CH1/CH0 搬运，`PE10` 片选；单端 `AIN0` 对 `GND`，连续模式 25 SPS，PGA ±2.048 V | 有符号 16 位原始码 | `Driver/GD30AD3344/GD30AD3344.c:220-236` |
| 电压换算 | `(int16_t)raw × 2.048 / 32768` | V | `Driver/GD30AD3344/GD30AD3344.c:251` |
| 电阻换算 | `V / 10.022 / 1.006 × 1000`（仪放增益 10.022、激励电流 1.006 mA） | Ω | `Function/Gd30ad3344_service.c:17`、`:23`、`:54-55` |
| 状态 | 底层 SPI/DMA 超时会清 `s_gd30ad3344_ready`；服务层同步到 `gd30ad3344_result.ready`，失败时保留上一帧电压与电阻值 | — | `Function/Gd30ad3344_service.c:42-46`、`:58` |
| 协议出口 | 直接返回 `gd30ad3344_result.resistance`，不乘变比 | Ω | `Function/Rs485_service.c:288-295` |

**已知边界：CH2 目前只输出电阻值（Ω），没有做 PT100 电阻到温度的换算。** 服务层注释明确说明温度曲线属于后续工作（`Function/Gd30ad3344_service.c:35-36`、`Function/Rs485_service.c:290-293`）。因此当前协议中 CH2 字段的量纲是 Ω 而不是 °C。

另外两点也需要如实说明：

- 采样任务周期 50 ms（20 Hz）与 ADC 配置的 25 SPS 并不对应，且没有「读取到的一定是新样本」的证据；一次服务调用读到的可能是同一转换结果。
- 协议出口未检查 `gd30ad3344_result.ready`，因此当 SPI/DMA 访问失败时，返回的是故障前最后一次有效电阻值而非错误标识（`Function/Rs485_service.c:294`）。这意味着上层无法从数值本身区分「实时值」与「过期值」。

### 6.4 统一协议出口与复用

`Rs485_Service_GetChannelValue(uint8_t channel)` 是三条链路的唯一出口（`Function/Rs485_service.c:278-298`）：CH0/CH1 返回 `平滑值 × 变比`，CH2 返回电阻值，其余通道返回 0.0f。它被三处复用：

- 单次查询命令 `0x0201` / `0x0202` / `0x0221`（`Function/Rs485_service.c:698-732`）；
- 自动上报帧 `0x0302` 的 CH0/CH1 字段（`Function/AutoReport_service.c:48-52`）；
- 告警检查 `Alert_Service_CheckChannel()` 的实测值（`Function/Alert_service.c:165`）。

自动上报帧固定 12 字节：4 字节 Unix 时间戳 + CH0 float + CH1 float，全部大端；源码注释明确说明该帧刻意不读取 CH2，以避免上报依赖外部 PT100 硬件（`Function/AutoReport_service.c:39-54`）。

## 7. 状态维护

### 7.1 参数持久化（`App_param_service`）

- 存储位置：共享参数页 `0x08010000`，参数块写在 `bl_param_t.reserved[]` 的固定偏移处，从而复用 BootLoader 已有的主/备双份 + CRC 校验机制（`Function/App_param_service.c:196-226`）。
- 覆盖内容：设备 ID、波特率编码、上报间隔编码、告警模式、CH0/CH1 变比、CH0/CH1/CH2 阈值、DAC 原始码（`Function/App_param_service.h:11-21` 及各 getter/setter 声明）。
- 加载规则：优先取主副本，主副本 CRC 无效时取备副本，两者都无效则写入默认值并立即提交（`Function/App_param_service.c:163-194`、`:228-240`）。注意此处选择规则是「主副本优先」，与 BootLoader 的「`update_counter` 较大者优先」不同。
- 提交方式：先把整页读到 RAM 缓存（`s_param_page_cache`），替换主/备两个 `bl_param_t`，再整页擦写并回写（`Function/App_param_service.c:218-223`）。主备两块位于同一 4 KiB 页内，因此擦除窗口内掉电可能同时丢失两份副本；这是有意的取舍，不是跨擦除单元的掉电保障。
- 依赖关系：`AppParam_Service_Init()` 依赖 `common/bl_param.h` 的结构布局，且必须在 DAC、告警、RS485 初始化之前执行。

### 7.2 告警记录（`Alert_service`）

- 记录区位于参数页内偏移 `+0x0600`、长度 1 KiB，独立于业务参数块与 BootLoader 日志区（`Function/Alert_service.c:13-14`）。
- 记录结构 `alert_store_t` 为环形缓冲，容量 10 条，每条包含时间戳、通道、阈值、实测值；整体带 magic、版本、序号、头指针、条数与 CRC32（`Function/Alert_service.c:16-35`）。
- 触发语义：只在「由未越限转为越限」的上升沿保存一条（`Function/Alert_service.c:158-174`）；初始化与清除后先抓取当前超阈状态作为基线，避免立刻补记（`Function/Alert_service.c:176-195`）。
- 提交方式与参数块相同：缓存整页 → 只替换告警区域 → 整页擦写（`Function/Alert_service.c:83-107`）。告警频繁发生时会造成参数页反复擦写，`SaveRecord()` 每次立即提交（`Function/Alert_service.c:147`）。
- 上报方式：告警模式为 `0x01` 时，保存后立刻通过 `Rs485_Service_SendRawText()` 直发 ASCII 文本行 `YYYY-MM-DD HH:MM:SS | CHx | threshold | actual`；历史查询逐行直发，无记录时发 `empty`（`Function/Alert_service.c:109-126`、`:215-233`）。
- 依赖关系：依赖 `AppParam_Service_GetThreshold()` 取阈值、依赖 `Rs485_Service_GetChannelValue()` 取实测值、依赖 `Rs485_Service_GetCurrentTimestamp()` 取时间、依赖 `Rs485_Service_SendRawText()` 输出。

### 7.3 睡眠与唤醒（`Sleep_service`）

- 请求路径：RS485 命令 `0x03AA` 先回 OK，再调用 `Sleep_Service_RequestRtcWakeup()` 只置请求标志与目标时刻（`Function/Rs485_service.c:858`、`Function/Sleep_service.c:132-136`）。
- 进入路径：`Sleep_Service_Task()` 在请求延时（20 ms）到期后调用 `Sleep_Service_EnterDeepSleep()`：停止自动上报、熄灭采集 LED、配置 RTC ALARM0（当前秒 +10 s，仅匹配秒字段）、关闭 OLED 显示，然后 `pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, PMU_LOWDRIVER_ENABLE, WFI_CMD)`（`Function/Sleep_service.c:97-123`）。
- 唤醒路径：RTC ALARM0 中断只由 `Sleep_Service_RtcAlarmIrqHandler()` 清标志置位（`Function/Sleep_service.c:154-162`）。WFI 返回后调用 `Sleep_Service_RestoreAfterWakeup()`：`SystemInit()` + `SystemCoreClockUpdate()` + `systick_config()` 恢复系统时钟与 1 ms 时基，再重新初始化 RS485 与 OLED，最后直发 `instrument wakeup`（`Function/Sleep_service.c:84-95`、`:119-122`）。睡醒后必须重配时钟，因为 deep sleep 唤醒后系统时钟回到内部 IRC16M。
- 依赖关系：睡眠依赖 RTC 硬件与 `EXTI_17`，唤醒恢复依赖 RS485 与 OLED 的重新初始化；睡眠期间其余任务全部挂起（`WFI` 期间不执行主循环）。

### 7.4 OLED 与 LED 指示

- **OLED**：`Oled_Service_Task()` 每 50 ms 刷新两行 16 px 文本。第一行固定显示队伍编号，第二行读 `AutoReport_Service_IsRunning()` 在 `AutoSample` 与 `IDLE` 之间切换；每行左侧补空格到 16 字符，避免短文本覆盖长文本后残留（`Function/Oled_service.c:26-65`）。显示内容依赖自动上报服务的运行状态，是唯一的跨服务 UI 依赖。
- **LED**：`led_state[LED_NUM]` 是业务与 GPIO 之间的唯一中间层，业务只改数组，`Led_Service_Task()` 负责把它映射到 `PE0/PE1/PE3/PE4/PE5/PE6`（`Function/Led_service.c:69-86`）。其中 LED1（索引 0）由自动上报服务置位表示采集中（`Function/AutoReport_service.c:29-35`），LED2（索引 1）由 `Led_Service_UpdateHeartbeat()` 每 1000 ms 翻转一次作为运行心跳（`Function/Led_service.c:26-42`），LED2~LED6 可由按键翻转。
- BootLoader 侧另有一套精简 OLED 显示（队伍编号 + `Bootloader`），在 `System_Init()` 中调用，用于升级期间的状态指示（`CIMC_BootLoader/Function/Function.c:10-11`）。

### 7.5 RTC

- 驱动层 `RTC_HwInit()` / `RTC_IsConfigured()` / `RTC_SetTime()` / `RTC_GetTime()` 负责备份域、时钟源与预分频，时间以 BCD 存储（`CIMC_App/Driver/RTC/RTC.h`）。
- 服务层 `RTC_Service_Task()` 只做健康监测：每 1000 ms 读一次秒值，秒值变化即认为走时正常并清零计数，连续 10 次不变则置 `rtc_healthy = 0`（`Function/Rtc_service.c:103-134`）。
- RTC 还是时间戳唯一来源：`Rs485_Service_GetCurrentTimestamp()` 把 BCD 时间换算为 Unix 秒（`Function/Rs485_service.c:341-483`），供自动上报帧头、告警记录时间与告警文本格式化（`Rs485_Service_FormatTimestamp()`）共用。
- RTC ALARM0 被睡眠服务复用为唤醒源（见 7.3）。睡眠路径只配置 ALARM0，不重置备份域，以免破坏已设置的时间与持久状态（`Function/Sleep_service.c:29-32`）。

## 8. 模块清单

下表按层列出各模块文件的职责与关键对外接口（`.c` `.h` 表示同目录下的实现与头文件）。

| 层 | 文件 | 职责 | 关键对外接口 |
|---|---|---|---|
| Driver | `Driver/ADC/ADC.c` `.h` | 内部 ADC0 初始化与单通道同步采样（`PC0`/`PC1`，12 位右对齐，采样时间 144 周期，软件触发） | `ADC_port_init()`、`ADC_Init()`、`ADC_ReadChannelRaw()`、`ADC_INTERNAL_CH0_INDEX`/`CH1_INDEX` |
| Driver | `Driver/DAC/DAC.c` `.h` | DAC0 OUT0（`PA4`）直流输出，关闭触发门控与波形模式，12 位右对齐，饱和到 4095 | `DAC_Init()`、`DAC_SetRaw()`、`DAC_GetRaw()`、`DAC_OUTPUT_MAX_RAW` |
| Driver | `Driver/GD30AD3344/GD30AD3344.c` `.h` | 外部 16 位 ADC 的 SPI3 + DMA1 收发、配置字组帧、片选与超时保护、原始码到电压换算 | `GD30AD3344_Init()`、`GD30AD3344_AD_Read()`、`GD30AD3344_IsReady()` |
| Driver | `Driver/RS485/RS485.c` `.h` | RS485（USART1）统一入口与编译期接收模式选择（`RS485_MODE_IDLE` / `RS485_MODE_DMA`，默认 DMA） | `RS485_Init()`、`RS485_SendBytes()`、`RS485_Printf()`、`RS485_FetchFrame()`、`RS485_IsBusy()` |
| Driver | `Driver/RS485/rs485_dma.c` `.h` | DMA0 CH5/CH6 收发、IDLE 分帧、DE 方向控制与 TC 时序；`USART1_IRQHandler` 在此实现 | `rs485_dma_init()`、`rs485_dma_send_bytes()`、`rs485_dma_fetch_frame()` |
| Driver | `Driver/RS485/rs485_idle.c` `.h` | 纯中断接收 + IDLE 分帧的实现（当前未选中的备用模式） | `rs485_idle_init()`、`rs485_idle_send_bytes()`、`rs485_idle_fetch_frame()` |
| Driver | `Driver/USART/USART.c` `.h` | USART0（`PA9`/`PA10`）统一入口、模式选择与帧接口 | `USART0_Init()`、`USART0_SendBytes()`、`USART0_Printf()`、`USART0_FetchFrame()`、`USART0_IsBusy()` |
| Driver | `Driver/USART/usart_dma.c` `.h` | USART0 DMA1 收发、32 KiB 接收缓冲、IDLE 分帧与升级流边界处理；`USART0_IRQHandler` 在此实现 | `usart_dma_init()`、`usart_dma_send_bytes()`、`usart_dma_fetch_frame()` |
| Driver | `Driver/USART/usart_idle.c` `.h` | USART0 纯中断接收实现（备用模式） | `usart_idle_init()` 等同族接口 |
| Driver | `Driver/OLED_IIC/OLED_IIC.c` `.h` | 软件模拟 I2C 驱动 128×32 OLED：字节/命令写、定位、字符/浮点/图片/汉字显示、开关显示 | `OLED_Init()`、`OLED_ShowStr()`、`OLED_ShowFloat()`、`OLED_Display_Off()` |
| Driver | `Driver/SPI_FLASH/SPI_FLASH.c` `.h` | 板载 SPI0 Flash 底层：JEDEC ID、页编程、扇区/范围擦除、缓冲读写、写等待 | `spi_flash_init()`、`spi_flash_read_id()`、`spi_flash_buffer_read/write/erase()` |
| Driver | `Driver/InternalFlash/InternalFlash.c` `.h` | 内部 Flash 页擦除、扇区擦除、按字节写入与按字节/字读取 | `InternalFlash_ErasePage()`、`InternalFlash_EraseSector()`、`InternalFlash_WriteBytes()`、`InternalFlash_ReadWord()` |
| Driver | `Driver/RTC/RTC.c` `.h` | RTC 备份域与时钟源初始化、BCD 时间读写、告警读取、复位源打印 | `RTC_HwInit()`、`RTC_SetTime()`、`RTC_GetTime()`、`RTC_IsConfigured()` |
| Driver | `Driver/TIMER/TIMER_BASIC.c` `.h` | TIM6 基本定时器（约 1 Hz 更新中断），ISR 内只清标志 | `Timer_Basic_Init()` |
| Driver | `Driver/TIMER/TIMER_PWM.c` `.h` | TIMER2_CH0 在 `PC6` 上的 PWM 输出与占空比设置 | `my_timer_init()`、`my_timer_set_duty()` |
| Driver | `Driver/LED/LED.c` `.h` | 6 路用户 LED（`PE0/PE1/PE3/PE4/PE5/PE6`）引脚定义与 GPIO 初始化 | `LED_Init()`、`LED_PORT`、`LED1_PIN`..`LED6_PIN`、`LED_NUM` |
| Driver | `Driver/KEY/KEY.c` `.h` | 6 路用户按键（`PD10`..`PD15`）引脚定义、GPIO 初始化与电平读取 | `KEY_Init()`、`KEY_Stat()` |
| Driver | `Driver/BootConfig/BootConfig.c` `.h` | 应用层视角的启动/升级配置结构：分区地址、更新标志与状态、`boot_param_t` 定义 | 分区与标志宏、`boot_param_t` 结构 |
| Function | `Function/Function.c` `.h` | 任务表 `poll_tasks`、`System_Init()` 初始化序列、`System_Loop()` 调度器、`UsrFunction()` 主循环 | `System_Init()`、`System_Loop()`、`UsrFunction()` |
| Function | `Function/Adc_service.c` `.h` | 内部 ADC 业务服务：CH0/CH1 原始码、mV 换算、两路独立 8 点滑动平均 | `Adc_Service_Init()`、`Adc_Service_Task()`、全局 `adc_result` |
| Function | `Function/Dac_service.c` `.h` | DAC 业务服务：上电恢复持久化码值，提供饱和后的设置/查询 | `Dac_Service_Init()`、`Dac_Service_SetRaw()`、`Dac_Service_GetRaw()` |
| Function | `Function/Gd30ad3344_service.c` `.h` | 外部 ADC 服务：选通道/量程、按仪放增益与激励电流换算 PT100 电阻、维护 ready 标志 | `Gd30ad3344_Service_Init()`、`Gd30ad3344_Service_Task()`、全局 `gd30ad3344_result` |
| Function | `Function/Rs485_service.c` `.h` | RS485 协议服务：帧归一化、ASCII-Hex 解码、帧校验、设备 ID 过滤、按命令族分发（`0x01xx`–`0x06xx`）、应答组帧、待复位与启动心跳 | `Rs485_Service_Init()`、`Rs485_Service_Task()`、`Rs485_Service_SendContent()`、`Rs485_Service_SendRawText()`、`Rs485_Service_GetChannelValue()`、`Rs485_Service_GetCurrentTimestamp()` |
| Function | `Function/AutoReport_service.c` `.h` | 自动上报状态机：按 1/3/5 s 间隔发送 12 字节 CH0/CH1 上报帧，维护采集 LED | `AutoReport_Service_Init()`、`AutoReport_Service_Start()`、`AutoReport_Service_Stop()`、`AutoReport_Service_Task()`、`AutoReport_Service_IsRunning()` |
| Function | `Function/Alert_service.c` `.h` | 告警监测与环形记录：越限上升沿保存、参数页持久化、主动文本上报、历史查询与清除 | `Alert_Service_Init()`、`Alert_Service_Task()`、`Alert_Service_SendHistoryText()`、`Alert_Service_Clear()` |
| Function | `Function/Sleep_service.c` `.h` | 低功耗服务：请求标志、RTC ALARM0 +10 s、进入 deep sleep、唤醒恢复时钟与外设、发送唤醒文本 | `Sleep_Service_Init()`、`Sleep_Service_Task()`、`Sleep_Service_RequestRtcWakeup()`、`Sleep_Service_RtcAlarmIrqHandler()` |
| Function | `Function/Oled_service.c` `.h` | OLED 状态页：第一行队伍编号，第二行 `AutoSample`/`IDLE` | `Oled_Service_Init()`、`Oled_Service_Task()` |
| Function | `Function/Led_service.c` `.h` | LED 状态层：`led_state[]` 映射到 GPIO，LED2 每 1 s 心跳翻转 | `Led_Service_Init()`、`Led_Service_Task()`、全局 `led_state[]` |
| Function | `Function/Key_service.c` `.h` | 按键服务：扫描 6 路按键，切换 PWM 占空比档位与 LED 状态 | `Key_Service_Init()`、`Key_Service_Task()` |
| Function | `Function/Rtc_service.c` `.h` | RTC 服务：硬件初始化与配置判断、秒值健康监测、时间/闹钟打印 | `RTC_Service_Init()`、`RTC_Service_Task()`、`RTC_Service_ShowTime()`、`RTC_Service_ShowAlarm()`、`rtc_healthy` |
| Function | `Function/Spi_flash_service.c` `.h` | 板载 SPI Flash 服务：初始化并校验 JEDEC ID，提供带就绪检查的读/写/擦除封装（当前无调用者，链接阶段被移除） | `SpiFlash_Service_Init()`、`SpiFlash_Service_IsReady()`、`SpiFlash_Service_Read/Write/Erase()` |
| Function | `Function/Usart_service.c` `.h` | USART0 服务：初始化并打印 Ready，取帧后优先交给 OTA、其次处理 `time` 调试命令 | `Usart_Service_Init()`、`Usart_Service_Task()`、`Usart_InputDecimal()` |
| Function | `Function/App_param_service.c` `.h` | 业务参数服务：设备 ID、波特率、变比、阈值、上报间隔、告警模式、DAC 码值的加载、校验与持久化 | `AppParam_Service_Init()`、各 `Get/Set` 接口、`AppParam_Service_BaudCodeToValue()` |
| Function | `Function/BootUpdate_service.c` `.h` | USART0 侧 OTA 接收：32 KiB 环形缓冲、流式包头识别、CRC32 累积、下载区擦写与状态提交、请求进入 BootLoader | `BootUpdate_Service_Init()`、`BootUpdate_Service_Task()`、`BootUpdate_Service_HandleFrame()`、`BootUpdate_Service_RequestEnterBootloader()` |
| Protocol | `Protocol/cimc_protocol.c` `.h` | 协议编解码与校验：ASCII-Hex 编解码、帧解析/组帧/OK 帧/错误帧、CRC16-Modbus、反射 CRC32、大端 16/32 位与 float 编码 | `CimcProtocol_DecodeAsciiHex()`、`CimcProtocol_ParseFrame()`、`CimcProtocol_BuildFrame()`、`CimcProtocol_Crc16Modbus()`、`CimcProtocol_Crc32Calc()`、`CimcProtocol_WriteFloatBe()` |
| Protocol | `Protocol/ota_protocol.c` `.h` | OTA 流式包头常量与合法性校验（App 侧） | `OtaProtocol_IsStreamHeaderValid()`、`OTA_FRAME_MAGIC` |
| 组件 | `Components/ringbuffer/ring_buffer.c` `.h` | 通用环形缓冲：初始化、可读/可写量、peek、read、drop、clear | `ring_buffer_init/available/free/peek/read/drop/clear()` |
| User | `User/systick.c` `.h` | 1 kHz SysTick 配置、阻塞延时、毫秒计数 | `systick_config()`、`delay_1ms()`、`tick_increment()`、`get_millis()` |
| User | `User/gd32f4xx_it.c` `.h` | 内核异常与 SysTick / USART / RTC 闹钟中断向量入口 | 各 `*_Handler()` / `*_IRQHandler()` |
| common | `common/bl_partition.h` | Flash 分区常量与相邻性/容量/越界编译期检查 | `BL_BOOT_*`、`BL_PARAM_*`、`BL_APP1_*`、`BL_DOWNLOAD_*` |
| common | `common/bl_param.h` | `bl_param_t`、`bl_log_entry_t`、magic/版本、更新标志与错误码定义 | `bl_param_t`、`BL_UPDATE_FLAG_*`、`BL_ERR_*` |
| CMSIS | `CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c` | 系统时钟配置（当前选中 240 MHz PLL @ 25 MHz 晶振） | `SystemInit()`、`SystemCoreClockUpdate()` |


## 9. 关键设计取舍与已知边界

### 9.1 协作式轮询 vs RTOS

取舍：项目选择「中断收集 + 固定顺序协作式轮询」，而不是引入 RTOS。

- 得到的好处：没有任务栈开销与上下文切换成本，调试时调用关系完全静态可读，与赛题「BootLoader/App 双工程、Driver/Protocol/Function 分层」的结构要求天然吻合。
- 付出的代价：没有优先级，也没有抢占。任何一个任务执行时间过长（或阻塞）都会推迟同一轮中它之后的所有任务。当前 `Rs485_Service_Task` 排在数组末尾（`Function.c:22`），因此通信响应最容易被前面任务的耗时所影响。
- 现实约束：`period_ms` 只是「再次调用的最小间隔」，不构成最坏响应时间保证。要给出响应时间承诺，必须先测量每个任务入口到出口的最坏执行时长与阻塞时长并叠加抖动，本项目尚未完成该标定（**待核实**）。因此本文不给出任何实测周期偏差、CPU 占用率或最坏响应时间数字。

### 9.2 单帧接收缓冲

RS485 接收路径在 IDLE 中断里把 DMA 缓冲整体拷贝到一个帧缓冲并置就绪标志（`Driver/RS485/rs485_dma.c:288-328`），主循环侧通过 `rs485_dma_fetch_frame()` 取走这一帧（`:243-274`）。全程只有**一个**帧缓冲，没有多帧队列：

- 如果上一帧尚未被 `Rs485_Service_Task()` 取走，下一个 IDLE 事件会直接覆盖它；
- 复制期间 DMA 被临时关闭（`:300`），这段窗口内到达的字节只能等下一次 IDLE 重新计数；
- 接收缓冲与帧缓冲均为 256 字节（`Driver/RS485/RS485.h:53-54`），超过容量由驱动侧截断。
- IDLE 是「链路出现空闲间隔」的电气事件，不是协议完整性保证。因此不能从「使用 DMA 接收」推导出「任意粘包/半包/超长输入都不会丢帧」。

### 9.3 阻塞式 DMA 发送

`rs485_dma_send_bytes()` 的时序是正确的（先拉高 DE → 清旧 TC 标志 → 启动 DMA → 等 FTF → 等 USART TC → 拉低 DE，`Driver/RS485/rs485_dma.c:173-237`），但两个等待循环没有超时上限：

- 一旦 USART 或 DMA 因外部原因未产生预期标志，函数会永久阻塞，进而卡死整个主循环；
- 发送期间（字节数 × 位时间）主循环不能执行任何其它任务，因此 RS485 应答本身就是一个可观测的阻塞点；
- 进入函数前还会等待上一次发送完成（`while (s_dma_tx_busy != 0U)`，`:189-191`），保证不可重入，但同样没有超时。

USART0 侧的 DMA 发送实现同样采用「等 FTF + 等 TC」的阻塞写法（`CIMC_App/Driver/USART/usart_dma.c`），差别只在不需要控制方向引脚。

### 9.4 Flash 擦写对实时性的影响

- 参数提交与告警保存都采用「读整页 → 改内存副本 → 擦整页 → 回写整页」的方式（`Function/App_param_service.c:218-223`、`Function/Alert_service.c:98-105`），每次提交都伴随一次完整的 4 KiB 页擦除与回写。
- `InternalFlash_WriteBytes()` / `InternalFlash_ErasePage()` 是同步等待实现；`BootUpdate_service.c` 中的 Flash 等待循环有计数值上限（`flash_wait_ready()`，`Function/BootUpdate_service.c:43-53`），但内部 Flash 驱动本身没有总超时（**待核实**：需按器件手册确认最坏擦写时间与超时策略）。
- 后果：任何一次参数写入或告警记录都会让主循环停顿一段可感知的时间，期间 RS485 接收依赖 DMA + IDLE 缓冲兜底，任务执行整体顺延。
- 相关的写入放大问题：告警在阈值附近来回抖动时会多次触发上升沿，每次触发都立即整页提交（`Function/Alert_service.c:147`），这既是实时性负担也是擦写寿命负担。逻辑环形缓冲不等于磨损均衡。

### 9.5 其它已知边界

- **CH2 只有电阻值，没有温度换算**（见 6.3），协议字段量纲与赛题最终要求的温度值不一致。
- **协议出口未检查采样就绪标志**：CH2 在 SPI/DMA 故障时返回最后一次有效值，上层无法从数值本身判断数据是否过期（见 6.3）。
- **自动上报只覆盖 CH0/CH1**：`AutoReport_Service_SendReportFrame()` 有意不读 CH2（`Function/AutoReport_service.c:46`）。
- 初始化阶段存在阻塞延时：`Adc_Service_Init()` 内的 `delay_1ms(50)`（`Function/Adc_service.c:144`）与 `ADC_Init()` 内的 `delay_1ms(1)`（`Driver/ADC/ADC.c:219`）不影响稳态调度，但会延长上电到首次通信应答的时间。
- `System_Init()` 在完成向量表切换后立即 `__enable_irq()`（`Function/Function.c:35`），后续各服务初始化过程中中断处于开启状态；若某个服务初始化依赖中断静默，需要单独确认（**待核实**）。
- `TIMER6_IRQHandler()` 只清更新标志，不承担任何业务动作（`Driver/TIMER/TIMER_BASIC.c:83-92`），是一个已启用但暂无功能的中断源；`RTC_Service_Task()` 在秒值未变化时会调用 `RTC_PrintResetSource()`（`Function/Rtc_service.c:124`），属于调试期遗留行为。
- 两工程共享的 `Protocol/` 与 `common/` 是物理副本，没有构建期一致性校验，改动一侧后必须手动同步另一侧。

## 依据

本文在编写过程中实际阅读过的文件（路径均相对仓库根目录）：

- `CIMC_App/Function/`：`Function.c/.h`、`Adc_service.c/.h`、`Dac_service.c/.h`、`Gd30ad3344_service.c/.h`
- `CIMC_App/Function/`：`Rs485_service.c/.h`、`AutoReport_service.c`、`Alert_service.c`、`Sleep_service.c`
- `CIMC_App/Function/`：`Oled_service.c`、`Led_service.c`、`Key_service.c`、`Rtc_service.c`、`Spi_flash_service.c`
- `CIMC_App/Function/`：`Usart_service.c`、`BootUpdate_service.c`、`App_param_service.c/.h`
- `CIMC_App/`：`HeaderFiles/HeaderFiles.h`、`User/main.c`、`User/systick.c`、`User/gd32f4xx_it.c`
- `CIMC_App/common/`：`bl_partition.h`、`bl_param.h`
- `CIMC_App/Protocol/`：`cimc_protocol.h`、`cimc_protocol.c`
- `CIMC_App/Driver/`：`ADC/ADC.c/.h`、`DAC/DAC.c/.h`、`GD30AD3344/GD30AD3344.c/.h`
- `CIMC_App/Driver/`：`RS485/RS485.h`、`RS485/rs485_dma.c`、`USART/USART.h`、`USART/usart_dma.c`（含内容检索）
- `CIMC_App/Driver/`：`OLED_IIC/OLED_IIC.h`、`SPI_FLASH/SPI_FLASH.c/.h`、`InternalFlash/InternalFlash.h`
- `CIMC_App/Driver/`：`RTC/RTC.h`、`TIMER/TIMER_BASIC.c/.h`、`TIMER/TIMER_PWM.c/.h`
- `CIMC_App/Driver/`：`LED/LED.h`、`KEY/KEY.h`、`BootConfig/BootConfig.h`
- `CIMC_App/`：`CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c`、`project/CIMC_App.uvprojx`
- `CIMC_BootLoader/`：`Function/bl_core.c`、`Function/Function.c`、`User/main.c`
- `CIMC_BootLoader/`：`Driver/USART/usart.c`、`Driver/OLED_IIC/bl_oled.c`、`project/CIMC_BootLoader.uvprojx`
- `docs/hardware-pinout.md`
- `docs/dev-notes/PROJECT_MEMORY.md`（背景参考；与源码冲突处以源码为准）
- 作者本人的项目问答稿（工程外资料，非本仓库文件）：用于交叉核对；其中标注为「设计推演」「改造方案」「待核实」的内容未写入本文的已实现部分
