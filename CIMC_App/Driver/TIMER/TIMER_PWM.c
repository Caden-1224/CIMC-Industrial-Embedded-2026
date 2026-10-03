#include "TIMER_PWM.h"

/************************* 宏定义 *************************/

/* 本模块固定把 PWM 输出放在 PC6，对应 TIMER2 的 CH0。 */
#define TIMER_PWM_PERIPH      TIMER2
#define TIMER_PWM_RCU_PERIPH  RCU_TIMER2
#define TIMER_PWM_CHANNEL     TIMER_CH_0
#define TIMER_PWM_GPIO_PORT   GPIOC
#define TIMER_PWM_GPIO_PIN    GPIO_PIN_6
#define TIMER_PWM_GPIO_AF     GPIO_AF_2
#define TIMER_PWM_RCU_GPIO    RCU_GPIOC

/* PSC = 239，分频后计数频率 = TIMERxCLK / 240。 */
#define TIMER_PWM_PRESCALER   (240U - 1U)

/************************ 函数声明 ************************/

static void timer_pwm_gpio_init(void);
static void timer_pwm_config(void);
static uint32_t timer_pwm_calc_pulse(uint8_t duty);

/**
 * @brief  初始化 PWM 输出模块。
 * @details 该函数是 PWM 模块的对外入口，按照“先配引脚、再配定时器”的顺序完成初始化，
 *          目的是先让 `PC6` 具备正确的复用输出能力，再启动 `TIMER2` 输出 PWM 波形。
 *
 * @param  void 无输入参数。
 * @return void 无返回值。
 */
void my_timer_init(void)
{
    /* 先把 PC6 切换为定时器复用输出脚，避免定时器已经开始输出而引脚仍停留在普通 GPIO 模式。 */
    timer_pwm_gpio_init();

    /* 再配置并启动 TIMER2，让外设真正开始按照设定频率和占空比逻辑输出 PWM。 */
    timer_pwm_config();
}

/**
 * @brief  设置 PWM 占空比。
 * @details 业务层传入百分比后，本函数负责把它换算成比较寄存器值，再写入 `TIMER2_CH0`。
 *
 * @param  duty 目标占空比，单位为百分比，推荐范围 `0~100`。
 * @return void 无返回值。
 */
void my_timer_set_duty(uint8_t duty)
{
    /* 先把百分比占空比换算为当前 ARR 配置下可直接写入 CCR 的比较值。 */
    uint32_t pulse_value = timer_pwm_calc_pulse(duty);

    /* 再把换算结果写入 CH0，比对值更新后，PWM 高电平宽度就会按新的占空比生效。 */
    timer_channel_output_pulse_value_config(TIMER_PWM_PERIPH, TIMER_PWM_CHANNEL, pulse_value);
}

/**
 * @brief  初始化 PWM 输出引脚。
 * @details 当前工程把 `PC6` 作为 `TIMER2_CH0` 输出脚使用。
 *          根据 GD32F470 的复用规则：
 *          - `PC6` 具备 `TIMER2_CH0` 复用功能；
 *          - `TIMER2` 属于 `GPIO_AF_2`；
 *          - 该脚当前未被本工程现有驱动模块占用，适合作为独立 PWM 测试口。
 *
 * @param  void 无输入参数。
 * @return void 无返回值。
 */
static void timer_pwm_gpio_init(void)
{
    /* 打开 GPIOC 外设时钟，否则后面对 PC6 的模式与复用配置不会真正写入硬件。 */
    rcu_periph_clock_enable(TIMER_PWM_RCU_GPIO);

    /* 把 PC6 设置为复用功能模式，让该引脚的输出控制权从 GPIO 转交给内部外设。 */
    gpio_mode_set(TIMER_PWM_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, TIMER_PWM_GPIO_PIN);

    /* 配置 PC6 为推挽输出和 50MHz 速度，保证 PWM 波形边沿具备稳定的驱动能力。 */
    gpio_output_options_set(TIMER_PWM_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, TIMER_PWM_GPIO_PIN);

    /* 给 PC6 绑定 AF2，把引脚内部的信号源明确切换到 TIMER2_CH0。 */
    gpio_af_set(TIMER_PWM_GPIO_PORT, TIMER_PWM_GPIO_AF, TIMER_PWM_GPIO_PIN);
}

/**
 * @brief  配置 TIMER2 的 PWM 工作参数。
 * @details 当前配置采用 `PWM 模式 1`、向上计数、边沿对齐。
 *          关键时间参数如下：
 *          - `PSC = 239`
 *          - `ARR = TIMER_PWM_ARR`
 *          - 初始 `CCR = 0`
 *
 *          因而 PWM 频率满足：
 *          `PWM_Freq = TIMER2CLK / ((PSC + 1) * (ARR + 1))`
 *
 *          当前工程时钟为 `240MHz SYSCLK`，并且在 `APB1 = AHB / 4`、`RCU_TIMER_PSC_MUL4`
 *          的条件下，`TIMER2CLK` 最终为 `240MHz`，所以本模块的 PWM 输出频率约为 `100Hz`。
 *
 * @param  void 无输入参数。
 * @return void 无返回值。
 */
static void timer_pwm_config(void)
{
    /* 该结构体负责承载定时器基础时基参数，例如 PSC、ARR、计数方向等。 */
    timer_parameter_struct timer_initpara;

    /* 该结构体负责承载输出比较参数，例如通道极性、输出使能和空闲态电平。 */
    timer_oc_parameter_struct timer_ocinitpara;

    /* 打开 TIMER2 外设时钟，否则后续所有定时器寄存器配置都不会生效。 */
    rcu_periph_clock_enable(TIMER_PWM_RCU_PERIPH);

    /* 按工程统一规则配置定时器时钟倍频关系，保证 TIMER2 获得预期的计数时钟来源。 */
    rcu_timer_clock_prescaler_config(RCU_TIMER_PSC_MUL4);

    /* 先把 TIMER2 恢复到缺省状态，避免继承上一次运行或调试残留的寄存器值。 */
    timer_deinit(TIMER_PWM_PERIPH);

    /* 用库函数默认值填充基础参数结构体，避免未显式赋值的字段携带随机值。 */
    timer_struct_para_init(&timer_initpara);

    /* 设置预分频值为 239，使定时器输入时钟先被 240 分频。 */
    timer_initpara.prescaler = TIMER_PWM_PRESCALER;

    /* 选择边沿对齐模式，这种模式最适合当前单路 LED 亮度调节场景。 */
    timer_initpara.alignedmode = TIMER_COUNTER_EDGE;

    /* 选择向上计数，使计数器从 0 递增到 ARR 后重新开始下一个周期。 */
    timer_initpara.counterdirection = TIMER_COUNTER_UP;

    /* 设置自动重装载值 ARR，用来确定一个 PWM 周期包含多少个计数节拍。 */
    timer_initpara.period = TIMER_PWM_ARR;

    /* 时钟细分保持 1 分频，本例不额外使用 CKDIV 去改变采样节奏。 */
    timer_initpara.clockdivision = TIMER_CKDIV_DIV1;

    /* 重复计数器置 0，表示每经过一个完整计数周期就产生一次更新事件。 */
    timer_initpara.repetitioncounter = 0U;

    /* 把以上基础参数一次性写入 TIMER2，建立后续 PWM 输出的时间基准。 */
    timer_init(TIMER_PWM_PERIPH, &timer_initpara);

    /* 用库函数默认值初始化输出比较结构体，先把未关心字段统一归零。 */
    timer_channel_output_struct_para_init(&timer_ocinitpara);

    /* 设置输出极性为高电平有效，这意味着 PWM 的“亮灯时间”对应高电平区间。 */
    timer_ocinitpara.ocpolarity = TIMER_OC_POLARITY_HIGH;

    /* 打开主通道输出使能，否则即便计数器在工作，引脚上也不会看到 PWM 波形。 */
    timer_ocinitpara.outputstate = TIMER_CCX_ENABLE;

    /* 互补通道极性保持库推荐值，虽然本例不使用互补输出，但保持参数完整更稳妥。 */
    timer_ocinitpara.ocnpolarity = TIMER_OCN_POLARITY_HIGH;

    /* 显式关闭互补输出，避免本次单路 LED 调光误触发额外的定时器输出通道。 */
    timer_ocinitpara.outputnstate = TIMER_CCXN_DISABLE;

    /* 空闲态主输出保持低电平，防止停止或未使能时引脚漂到高电平。 */
    timer_ocinitpara.ocidlestate = TIMER_OC_IDLE_STATE_LOW;

    /* 互补输出空闲态同样配置为低电平，保证整个通道组的空闲行为一致。 */
    timer_ocinitpara.ocnidlestate = TIMER_OCN_IDLE_STATE_LOW;

    /* 把通道级输出参数应用到 CH0，完成 PWM 通道的极性与输出使能配置。 */
    timer_channel_output_config(TIMER_PWM_PERIPH, TIMER_PWM_CHANNEL, &timer_ocinitpara);

    /* 把初始比较值设为 0，使模块上电后默认从 0% 占空比开始。 */
    timer_channel_output_pulse_value_config(TIMER_PWM_PERIPH, TIMER_PWM_CHANNEL, 0U);

    /* 把 CH0 配置为 PWM 模式 1，使计数值与比较值的关系转换成标准 PWM 波形。 */
    timer_channel_output_mode_config(TIMER_PWM_PERIPH, TIMER_PWM_CHANNEL, TIMER_OC_MODE_PWM1);

    /* 打开通道影子寄存器，让占空比更新在更新事件时统一装载，避免半周期撕裂。 */
    timer_channel_output_shadow_config(TIMER_PWM_PERIPH, TIMER_PWM_CHANNEL, TIMER_OC_SHADOW_ENABLE);

    /* 打开 ARR 影子寄存器，确保周期参数在需要更新时也能以同步方式生效。 */
    timer_auto_reload_shadow_enable(TIMER_PWM_PERIPH);

    /* 最后启动 TIMER2 计数器，到这里为止，PC6 就会开始持续输出当前 PWM 波形。 */
    timer_enable(TIMER_PWM_PERIPH);
}

/**
 * @brief  将百分比占空比换算为比较值。
 * @details 该函数集中负责两件事：
 *          1. 对大于 `100%` 的非法输入做钳制；
 *          2. 按 `ARR * duty / 100` 的公式计算出可写入 `CCR` 的数值。
 *
 * @param  duty 业务层传入的占空比百分比。
 * @return uint32_t 可直接写入比较寄存器的脉冲值。
 */
static uint32_t timer_pwm_calc_pulse(uint8_t duty)
{
    /* 先把 8 位输入提升到 32 位，避免后续乘法运算时出现不必要的类型截断。 */
    uint32_t limited_duty = duty;

    /* 如果业务层传入了超过 100% 的非法值，就在驱动层直接把它压到最大合法范围。 */
    if (limited_duty > TIMER_PWM_MAX_DUTY_PERCENT)
    {
        /* 钳制到 100% 后，可以避免比较值越界到 ARR 之外，保持 PWM 行为可预期。 */
        limited_duty = TIMER_PWM_MAX_DUTY_PERCENT;
    }

    /* 按比例计算最终应写入 CCR 的比较值，用它决定一个周期内高电平持续多久。 */
    return (limited_duty * TIMER_PWM_ARR) / TIMER_PWM_MAX_DUTY_PERCENT;
}
