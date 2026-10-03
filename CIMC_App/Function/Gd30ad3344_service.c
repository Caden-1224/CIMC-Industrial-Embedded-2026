#include "Gd30ad3344_service.h"
#include "GD30AD3344.h"

/*
 * 当前 PT100 测量通道。
 *
 * 源工程中使用 GD30AD3344_Channel_4，即 AIN0 对 GND 的单端采样。
 * 如果后续硬件把 PT100 前端接到别的 AIN 引脚，只需要改这里，不需要改调度层。
 */
#define GD30AD3344_SERVICE_CHANNEL       GD30AD3344_Channel_4

/*
 * 当前采样量程。
 *
 * GD30AD3344_PGA_2V048 表示满量程约 +/-2.048V。量程越小分辨率越高，
 * 但输入电压超过量程会饱和；这里沿用源工程配置。
 */
#define GD30AD3344_SERVICE_RANGE         GD30AD3344_PGA_2V048

/*
 * PT100 模拟前端的仪表放大器增益。
 *
 * GD30AD3344 读到的是放大后的电压，因此换算电阻前要先除以前端增益。
 */
#define GD30AD3344_SERVICE_INA_GAIN      10.022f

/*
 * PT100 激励电流，单位 mA。
 *
 * 电阻计算公式本质是 R = U / I。这里电流用 mA 表示，所以后面计算时
 * 需要乘以 1000，把 V/mA 换成 ohm。
 */
#define GD30AD3344_SERVICE_RTD_CURRENT   1.006f

/*
 * 服务层缓存的最新外部 ADC 结果。
 *
 * 这个结构体是有意对外暴露的：本模板里的服务层通常比较薄，
 * 其他显示/通信模块直接读取结果即可，不再额外维护 getter 函数。
 */
Gd30ad3344_Result_t gd30ad3344_result = {0.0f, 0.0f, 0U};

/**
 * @brief 刷新一次 GD30AD3344/PT100 采样结果。
 *
 * @details
 * 该函数是本服务层的核心私有逻辑：
 *   1. 先检查底层 GD30AD3344 驱动是否 ready。
 *   2. 读取指定通道的电压值，单位 V。
 *   3. 根据前端增益和激励电流换算 PT100 电阻。
 *   4. 更新 gd30ad3344_result，供上层显示或通信任务读取。
 *
 * 注意：
 *   - 这里不直接操作 SPI/DMA 寄存器，那些细节属于 HardWare 层。
 *   - 这里也不做温度查表，只输出直接电阻值，便于后续按不同 PT100
 *     标定曲线或业务需求再扩展温度换算。
 */
static void Gd30ad3344_Service_Update(void)
{
    float voltage;

    /* 底层若已经标记不可用，服务层只清 ready，不覆写上一帧电压/电阻。
       这样调试时仍能看到故障前最后一次有效结果。 */
    if (GD30AD3344_IsReady() == 0U)
    {
        gd30ad3344_result.ready = 0U;
        return;
    }

    /* GD30AD3344_AD_Read() 返回已经按 PGA 量程换算后的电压值，单位 V。 */
    voltage = GD30AD3344_AD_Read(GD30AD3344_SERVICE_CHANNEL, GD30AD3344_SERVICE_RANGE);

    gd30ad3344_result.voltage = voltage;

    /* R = U / gain / I。由于 I_RTD 用 mA 保存，所以乘 1000 得到 ohm。 */
    gd30ad3344_result.resistance =
        (voltage / GD30AD3344_SERVICE_INA_GAIN / GD30AD3344_SERVICE_RTD_CURRENT) * 1000.0f;

    /* 读数过程中如果 SPI/DMA 超时，底层会把 ready 清 0，这里同步到服务结果。 */
    gd30ad3344_result.ready = GD30AD3344_IsReady();
}

/**
 * @brief 初始化 GD30AD3344 服务层。
 */
void Gd30ad3344_Service_Init(void)
{
    /* 底层初始化会完成 SPI3、DMA、GPIO、CS 和 GD30AD3344 默认配置字写入。 */
    GD30AD3344_Init();

    /* 初始化后立刻采样一次，让显示/通信模块启动时不必面对全 0 的占位数据。 */
    Gd30ad3344_Service_Update();
}

/**
 * @brief GD30AD3344 服务层周期任务。
 */
void Gd30ad3344_Service_Task(void)
{
    /* 保持任务函数足够薄，调度器只需要周期调用它即可。 */
    Gd30ad3344_Service_Update();
}
