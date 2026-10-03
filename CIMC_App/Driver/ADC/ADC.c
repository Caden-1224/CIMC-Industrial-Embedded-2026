/************************* 头文件 *************************/

#include "ADC.h"

/************************* 宏定义 *************************/

/************************ 变量定义 ************************/

#define ADC_READ_TIMEOUT_LOOP 100000UL


/************************ 函数定义 *************************/

/**
 * ============================================================
 *  ADC 入门知识 — 读这里就够了
 * ============================================================
 *
 * 【ADC 是什么】
 *   ADC (Analog-to-Digital Converter) 把连续的模拟电压转换成离散的数字量。
 *   GD32F4xx 的 ADC 是 12 位精度，转换结果范围 0 ~ 4095，对应输入电压 0 ~ VREF。
 *
 * 【关键概念速查】
 *
 *   1. 分辨率 (Resolution)
 *      12 bit → 2^12 = 4096 级，能分辨 VREF/4096 的电压变化。
 *      例：VREF = 3.3V 时，最小分辨 ≈ 0.8mV。
 *
 *   2. 采样时间 (Sample Time)
 *      ADC 内部有一个采样保持电容，采样时间决定了电容充电多久。
 *      信号源内阻越大，需要的采样时间越长，否则结果会偏低。
 *      本工程使用 ADC_SAMPLETIME_56（56 个 ADC 时钟周期）。
 *
 *   3. 转换模式
 *      - 单次模式 (Single)：触发一次，转换一次，然后停止。
 *      - 连续模式 (Continuous)：触发一次后自动循环转换，永不停止。
 *        本工程使用连续模式。
 *
 *   4. 触发源 (Trigger Source)
 *      ADC 可以由以下方式启动转换：
 *      - 软件触发：代码中调用 adc_software_trigger_enable()
 *      - 外部触发：定时器输出比较、外部中断引脚等
 *        本工程配置了 Timer0_CH3 外部触发，同时在初始化末尾做一次软件触发来"点火"。
 *
 *   5. 数据对齐 (Alignment)
 *      - 右对齐 (Right)：结果在低 12 位，直接读取即可。
 *      - 左对齐 (Left)：结果左移到高 12 位，需 >>4 才是实际值。
 *        本工程使用右对齐。
 *
 *   6. 规则组 vs 注入组
 *      - 规则组 (Regular Group)：最多 16 个通道，按顺序依次转换。
 *      - 注入组 (Injected Group)：最多 4 个通道，可打断规则组优先转换（类似中断）。
 *        本工程仅使用规则组，1 个通道。
 *
 *   7. 通道映射 (Channel → GPIO)
 *      ADC 通道号与 GPIO 引脚有固定映射关系（见芯片数据手册）：
 *        ADC_CHANNEL_10 → PC0    （本工程使用的通道）
 *        ADC_CHANNEL_11 → PC1
 *        ADC_CHANNEL_12 → PC2
 *        ADC_CHANNEL_13 → PC3
 *        ADC_CHANNEL_0  ~ 7  → PA0 ~ PA7
 *        ADC_CHANNEL_8  ~ 9  → PB0 ~ PB1
 *
 *   8. DMA 模式（当你取消 ADC_USE_DMA 宏时启用）
 *      DMA (Direct Memory Access) 是片上硬件搬运工。
 *      开启后，ADC 每转换完一个数据，DMA 自动把它从 ADC 数据寄存器
 *      搬运到内存数组 adc_dma_buffer[]，全程不需要 CPU 参与任何读写。
 *
 *      典型数据流：
 *        模拟电压 → ADC转换 → ADC数据寄存器 →[DMA自动搬运]→ adc_dma_buffer[]
 *
 *   9. 如何读取 ADC 值
 *      - 轮询模式：uint16_t val = adc_routine_data_read(ADC0);
 *      - DMA 模式 ：uint16_t val = adc_dma_buffer[0];
 *        读取后换算电压：voltage = val * 3.3 / 4095.0;
 *
 * ============================================================
 */


/************************************************************
 * Function :  ADC_port_init
 * Comment  :  ADC 外设端口初始化。完成 GPIO / 时钟 / DMA(可选) 配置，
 *             最后触发 ADC 启动连续转换。
 * Parameter:  null
 * Return   :  null
 * Author   :  Lingyu Meng  2025-02-30 V0.1 original
 *            Caden         2026-04-17 V0.2 add DMA mode via ifdef
 ************************************************************/

void ADC_port_init(void)
{
    /*-------- 第一部分：时钟使能 --------*/

    rcu_periph_clock_enable(RCU_GPIOC);  // GPIOC 时钟 — PC0/PC1 需要时钟才能配置为模拟输入
    rcu_periph_clock_enable(RCU_ADC0);   // ADC0 时钟 — 使能后才能操作 ADC 寄存器


    /*-------- 第二部分：GPIO 配置 --------*/

    /*
     * 将 PC0/PC1 设为模拟输入模式 (GPIO_MODE_ANALOG)。
     * PC0 是赛题 CH0 板载电位器；PC1 是赛题 CH1 DAC 回读输入。
     * 模拟模式下，引脚直连 ADC 内部采样电路，数字输入施密特触发器被关闭，
     * 这样可以得到最纯净的模拟信号。
     * GPIO_PUPD_NONE — 不启用上拉/下拉电阻，避免对模拟信号引入偏置电压。
     */
    gpio_mode_set(GPIOC, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO_PIN_0 | GPIO_PIN_1);

    /*-------- 第三部分：ADC 时钟预分频 --------*/

    /*
     * ADC 时钟来源于 APB2 (PCLK2)。
     * DIV8 表示 ADC 时钟 = APB2 / 8。
     * GD32F4 的 ADC 时钟最高不超过 40MHz，分频值需根据 APB2 频率选择。
     * 例：当前工程 APB2 = 120MHz 时，DIV8 → 15MHz，满足要求。
     */
    adc_clock_config(ADC_ADCCK_PCLK2_DIV8);

    /*-------- 第四部分：ADC 核心配置 --------*/

    ADC_Init();

    /*-------- 第五部分（DMA 专属）：DMA 通道配置 --------*/


    /*-------- 第六部分：启动 ADC 转换 --------*/

    /*
     * 软件触发一次来"点火"。
     * 因为配置了连续模式，这一脚踢完之后 ADC 就会不停地循环转换。
     * - 轮询模式下，每次转换的结果存在 ADC_RDATA 寄存器中，等待 CPU 读取。
     * - DMA 模式下，每次转换的结果由 DMA 自动搬运到 adc_dma_buffer[]。
     */
    /* ???????? ADC_ReadChannelRaw() ??????????? */
}


/************************************************************
 * Function :  ADC_Init
 * Comment  :  ADC 核心参数配置。在 ADC_port_init() 内部被调用，
 *             负责转换模式 / 对齐方式 / 通道 / 触发源的设置。
 * Parameter:  null
 * Return   :  null
 * Author   :  Lingyu Meng  2025-02-30 V0.1 original
 *            Caden         2026-04-17 V0.2 add DMA mode via ifdef
 ************************************************************/

void ADC_Init(void)
{
    /* 复位 ADC 所有寄存器到默认值，防止残留配置干扰 */
    adc_deinit();

    /*
     * 连续转换模式：ENABLE
     * ADC 完成一次转换后立刻自动开始下一次，无需反复触发。
     * 如果只需要单次转换，将 ENABLE 改为 DISABLE 即可。
     */
    adc_special_function_config(ADC0, ADC_CONTINUOUS_MODE, DISABLE);

    /*
     * 扫描模式：ENABLE
     * 规则组包含 CH0 和 CH1 两个 rank，必须按 rank0 -> rank1 的顺序扫描一轮。
     * DMA 缓冲区顺序依赖这个 rank 顺序，因此这里是三通道业务映射的关键配置。
     */
    adc_special_function_config(ADC0, ADC_SCAN_MODE, DISABLE);

    /*
     * 数据右对齐：ADC_DATAALIGN_RIGHT
     * 12 位转换结果放在 16 位寄存器的 bit[11:0]，高位补 0。
     * 直接读取就是 0 ~ 4095 的真实 ADC 值。
     */
    adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);
    adc_end_of_conversion_config(ADC0, ADC_EOC_SET_CONVERSION);

    /*
     * 规则组通道数量 = 2
     * 一轮扫描依次转换 CH0 与 CH1，DMA 按相同顺序写入 adc_dma_buffer[0..1]。
     */
    adc_channel_length_config(ADC0, ADC_ROUTINE_CHANNEL, 1U);

    /*
     * 规则组第 0 个通道的详细配置：
     *   参数 2 — ADC_INTERNAL_CH0_INDEX：rank0，DMA 对应 adc_dma_buffer[0]
     *   参数 3 — ADC_CHANNEL_10：通道号，对应 PC0 引脚，即赛题 CH0 板载电位器
     *   参数 4 — ADC_SAMPLETIME_56：采样时间 56 个 ADC 时钟周期
     *
     * 采样时间选择原则：
     *   信号源内阻越小，采样时间可以越短。
     *   一般传感器输出阻抗 < 10kΩ，56 周期足够。
     *   如果信号源阻抗高或对精度要求高，可增大到 112 / 144 / 480。
     */
    adc_routine_channel_config(ADC0, ADC_INTERNAL_CH0_INDEX, ADC_CHANNEL_10, ADC_SAMPLETIME_56);

    /*
     * 规则组第 1 个通道的详细配置：
     *   参数 2 — ADC_INTERNAL_CH1_INDEX：rank1，DMA 对应 adc_dma_buffer[1]
     *   参数 3 — ADC_CHANNEL_11：通道号，对应 PC1 引脚，即赛题 CH1 DAC 回读
     *
     * 赛题要求 PA4 跳接至 PC1，因此这里读取真实回读电压，而不是直接复用 DAC 寄存器值。
     */
    /* CH1 ? ADC_ReadChannelRaw() ?????? ADC_CHANNEL_11? */

    /*
     * 外部触发源配置：Timer0 Channel0 的输出比较信号。
     * 这样可以用定时器精确控制 ADC 采样率。
     *   例：定时器 1kHz → ADC 每秒采样 1000 次。
     * 注意：规则组必须使用 ADC_EXTTRIG_ROUTINE_T0_CHx，不能用 INSERTED 系列宏。
     */
    adc_external_trigger_source_config(ADC0, ADC_ROUTINE_CHANNEL, ADC_EXTTRIG_ROUTINE_T0_CH0);

    /* 关闭外部触发，使用软件触发 + 连续模式 */
    adc_external_trigger_config(ADC0, ADC_ROUTINE_CHANNEL, DISABLE);

    /* 使能 ADC 接口 —— 配置生效的前提 */
    adc_enable(ADC0);

    /* 等待 ADC 稳定（至少 1ms） */
    delay_1ms(1);

    /*
     * ADC 校准：内部自动修正偏移误差，提高转换精度。
     * 必须在 adc_enable() 之后调用，且只需校准一次。
     */
    adc_calibration_enable(ADC0);
}

uint16_t ADC_ReadChannelRaw(uint8_t adc_channel)
{
    uint32_t timeout = ADC_READ_TIMEOUT_LOOP;
    uint16_t raw_value;

    adc_channel_length_config(ADC0, ADC_ROUTINE_CHANNEL, 1U);
    adc_routine_channel_config(ADC0, 0U, adc_channel, ADC_SAMPLETIME_144);

    adc_flag_clear(ADC0, ADC_FLAG_EOC | ADC_FLAG_ROVF);
    adc_software_trigger_enable(ADC0, ADC_ROUTINE_CHANNEL);

    while ((adc_flag_get(ADC0, ADC_FLAG_EOC) == RESET) && (timeout > 0UL))
    {
        timeout--;
    }

    raw_value = adc_routine_data_read(ADC0);
    adc_flag_clear(ADC0, ADC_FLAG_EOC | ADC_FLAG_ROVF);
    return raw_value;
}
