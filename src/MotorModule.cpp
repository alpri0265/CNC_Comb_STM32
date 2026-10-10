#include "MotorModule.h"

namespace MotorModule {

struct AxisState {
    TIM_HandleTypeDef htim;
    volatile int32_t  pos_pulses;
    volatile int32_t  pulses_left;
    volatile bool     dir_positive;
    volatile bool     busy;
    GPIO_TypeDef *     dir_port;
    uint16_t           dir_pin;
    GPIO_TypeDef *     ena_port;
    uint16_t           ena_pin;
    uint32_t           timer_clk_hz;
    uint32_t           channel;
};

static AxisState s_axis[3];

static void init_timer_pwm(TIM_HandleTypeDef *htim, TIM_TypeDef *instance,
                           uint32_t timer_clk_hz, uint32_t channel,
                           GPIO_TypeDef *port, uint16_t pul_pin, uint8_t af);

void init()
{
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    // X: DIR PE10, ENA PE11
    gpio.Pin = PinCfg::X_DIR_PIN | PinCfg::X_ENA_PIN;
    HAL_GPIO_Init(PinCfg::X_GPIOx, &gpio);
    HAL_GPIO_WritePin(PinCfg::X_GPIOx, PinCfg::X_DIR_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PinCfg::X_GPIOx, PinCfg::X_ENA_PIN, GPIO_PIN_SET); // disabled HIGH (safe)

    // Y: DIR PB5, ENA PB8
    gpio.Pin = PinCfg::Y_DIR_PIN | PinCfg::Y_ENA_PIN;
    HAL_GPIO_Init(PinCfg::Y_GPIOx, &gpio);
    HAL_GPIO_WritePin(PinCfg::Y_GPIOx, PinCfg::Y_DIR_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PinCfg::Y_GPIOx, PinCfg::Y_ENA_PIN, GPIO_PIN_SET);

    // Z: DIR PD13, ENA PD14, BRAKE PD15
    gpio.Pin = PinCfg::Z_DIR_PIN | PinCfg::Z_ENA_PIN | PinCfg::Z_BRAKE_PIN;
    HAL_GPIO_Init(PinCfg::Z_GPIOx, &gpio);
    HAL_GPIO_WritePin(PinCfg::Z_GPIOx, PinCfg::Z_DIR_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PinCfg::Z_GPIOx, PinCfg::Z_ENA_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PinCfg::Z_GPIOx, PinCfg::Z_BRAKE_PIN, GPIO_PIN_SET); // brake applied initially

    // X PUL PE9 AF1
    init_timer_pwm(&s_axis[0].htim, TIM1, AxisCfg::APB2_TIM_CK, TIM_CHANNEL_1,
                   PinCfg::X_GPIOx, PinCfg::X_PUL_PIN, GPIO_AF1_TIM1);
    s_axis[0].dir_port    = PinCfg::X_GPIOx;
    s_axis[0].dir_pin     = PinCfg::X_DIR_PIN;
    s_axis[0].ena_port    = PinCfg::X_GPIOx;
    s_axis[0].ena_pin     = PinCfg::X_ENA_PIN;
    s_axis[0].channel     = TIM_CHANNEL_1;
    s_axis[0].timer_clk_hz= AxisCfg::APB2_TIM_CK;

    // Y PUL PB4 AF2
    init_timer_pwm(&s_axis[1].htim, TIM3, AxisCfg::APB1_TIM_CK, TIM_CHANNEL_1,
                   PinCfg::Y_GPIOx, PinCfg::Y_PUL_PIN, GPIO_AF2_TIM3);
    s_axis[1].dir_port    = PinCfg::Y_GPIOx;
    s_axis[1].dir_pin     = PinCfg::Y_DIR_PIN;
    s_axis[1].ena_port    = PinCfg::Y_GPIOx;
    s_axis[1].ena_pin     = PinCfg::Y_ENA_PIN;
    s_axis[1].channel     = TIM_CHANNEL_1;
    s_axis[1].timer_clk_hz= AxisCfg::APB1_TIM_CK;

    // Z PUL PD12 AF2
    init_timer_pwm(&s_axis[2].htim, TIM4, AxisCfg::APB1_TIM_CK, TIM_CHANNEL_1,
                   PinCfg::Z_GPIOx, PinCfg::Z_PUL_PIN, GPIO_AF2_TIM4);
    s_axis[2].dir_port    = PinCfg::Z_GPIOx;
    s_axis[2].dir_pin     = PinCfg::Z_DIR_PIN;
    s_axis[2].ena_port    = PinCfg::Z_GPIOx;
    s_axis[2].ena_pin     = PinCfg::Z_ENA_PIN;
    s_axis[2].channel     = TIM_CHANNEL_1;
    s_axis[2].timer_clk_hz= AxisCfg::APB1_TIM_CK;

    for (int i = 0; i < 3; i++) {
        s_axis[i].pos_pulses   = 0;
        s_axis[i].pulses_left  = 0;
        s_axis[i].dir_positive = true;
        s_axis[i].busy         = false;
    }

    HAL_NVIC_SetPriority(TIM1_UP_TIM10_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn);
    HAL_NVIC_SetPriority(TIM3_IRQn, 2, 1);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    HAL_NVIC_SetPriority(TIM4_IRQn, 2, 2);
    HAL_NVIC_EnableIRQ(TIM4_IRQn);
}

static void init_timer_pwm(TIM_HandleTypeDef *htim, TIM_TypeDef *instance,
                           uint32_t timer_clk_hz, uint32_t channel,
                           GPIO_TypeDef *port, uint16_t pul_pin, uint8_t af)
{
    (void)timer_clk_hz; // ARR/PSC обчислюються динамічно в apply_step_freq (див. нижче)
    GPIO_InitTypeDef gpio = {};
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = af;
    gpio.Pin       = pul_pin;
    HAL_GPIO_Init(port, &gpio);

    htim->Instance               = instance;
    htim->Init.Prescaler         = 0;
    htim->Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim->Init.Period            = 999;
    htim->Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim->Init.RepetitionCounter = 0;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(htim);

    TIM_OC_InitTypeDef oc = {};
    oc.OCMode       = TIM_OCMODE_PWM1;
    oc.Pulse        = 500;
    oc.OCPolarity   = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode   = TIM_OCFAST_DISABLE;
    oc.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    oc.OCIdleState  = TIM_OCIDLESTATE_RESET;
    HAL_TIM_PWM_ConfigChannel(htim, &oc, channel);
}

static void apply_step_freq(AxisState &a, uint32_t step_freq_hz)
{
    if (step_freq_hz < 1) step_freq_hz = 1;
    const uint32_t ARR_TARGET = 100;
    uint32_t psc = a.timer_clk_hz / (ARR_TARGET + 1) / step_freq_hz;
    if (psc > 0xFFFF) psc = 0xFFFF;
    uint32_t arr = a.timer_clk_hz / (psc + 1) / step_freq_hz;
    if (arr < 1)   arr = 1;
    if (arr > 0xFFFF) arr = 0xFFFF;
    uint32_t ccr   = arr / 2;
    __HAL_TIM_SET_PRESCALER(&a.htim, psc);
    __HAL_TIM_SET_AUTORELOAD(&a.htim, arr);
    __HAL_TIM_SET_COMPARE(&a.htim, a.channel, ccr);
    __HAL_TIM_SET_COUNTER(&a.htim, 0);
}

void enable_all(bool on)
{
    for (int i = 0; i < 3; i++) enable_axis((Axis)i, on);
    // Z brake logic: only release when motion enabled
    HAL_GPIO_WritePin(PinCfg::Z_GPIOx, PinCfg::Z_BRAKE_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void enable_axis(Axis a, bool on)
{
    if ((int)a < 0 || (int)a >= 3) return;
    HAL_GPIO_WritePin(s_axis[(int)a].ena_port, s_axis[(int)a].ena_pin,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void set_direction(Axis a, bool positive)
{
    if ((int)a < 0 || (int)a >= 3) return;
    AxisState &ax = s_axis[(int)a];
    ax.dir_positive = positive;
    HAL_GPIO_WritePin(ax.dir_port, ax.dir_pin, positive ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool get_direction(Axis a)
{
    if ((int)a < 0 || (int)a >= 3) return false;
    return s_axis[(int)a].dir_positive;
}

void move_pulses(Axis a, int32_t pulses, uint32_t step_freq_hz)
{
    if ((int)a < 0 || (int)a >= 3) return;
    AxisState &ax = s_axis[(int)a];
    if (pulses == 0) { ax.busy = false; return; }

    bool pos = (pulses > 0);
    int32_t abs_p = (pulses < 0) ? -pulses : pulses;

    set_direction(a, pos);
    apply_step_freq(ax, step_freq_hz);

    __disable_irq();
    ax.pulses_left = abs_p;
    ax.busy        = true;
    __enable_irq();

    TIM_CCxChannelCmd(ax.htim.Instance, ax.channel, TIM_CCx_ENABLE);
    __HAL_TIM_ENABLE_IT(&ax.htim, TIM_IT_UPDATE);
    HAL_TIM_PWM_Start(&ax.htim, ax.channel);
    if (ax.htim.Instance == TIM1) {
        __HAL_TIM_MOE_ENABLE(&ax.htim);
    }
}

void move_mm(Axis a, float delta_mm, float feed_mm_min)
{
    int32_t pulses = (int32_t)(delta_mm * AxisCfg::PULSES_PER_MM + 0.5f * ((delta_mm >= 0) ? 1 : -1));
    if (feed_mm_min < 0.5f) feed_mm_min = 0.5f;
    uint32_t freq = (uint32_t)(feed_mm_min * AxisCfg::PULSES_PER_MM / 60.0f + 0.5f);
    move_pulses(a, pulses, freq);
}

void move_absolute_mm(Axis a, float abs_mm, float feed_mm_min)
{
    float cur = get_position_mm(a);
    move_mm(a, abs_mm - cur, feed_mm_min);
}

void abort(Axis a)
{
    if ((int)a < 0 || (int)a >= 3) return;
    AxisState &ax = s_axis[(int)a];
    __HAL_TIM_DISABLE_IT(&ax.htim, TIM_IT_UPDATE);
    HAL_TIM_PWM_Stop(&ax.htim, ax.channel);
    // MOE (Main Output Enable) існує ТІЛЬКИ в advanced таймері TIM1 (BDTR регістр).
    // Для general-purpose TIM3/TIM4: зарезервований регістр, запис = непередбачувана поведінка.
    if (ax.htim.Instance == TIM1) {
        __HAL_TIM_MOE_DISABLE(&ax.htim);
    }
    __disable_irq();
    ax.pulses_left = 0;
    ax.busy        = false;
    __enable_irq();
}

void abort_all()
{
    for (int i = 0; i < 3; i++) abort((Axis)i);
}

bool is_busy(Axis a)
{
    if ((int)a < 0 || (int)a >= 3) return false;
    return s_axis[(int)a].busy;
}

bool any_busy()
{
    for (int i = 0; i < 3; i++) if (s_axis[i].busy) return true;
    return false;
}

int32_t get_position_pulses(Axis a)
{
    if ((int)a < 0 || (int)a >= 3) return 0;
    return s_axis[(int)a].pos_pulses;
}

float get_position_mm(Axis a)
{
    return (float)get_position_pulses(a) * AxisCfg::MM_PER_PULSE;
}

void reset_position(Axis a, float mm)
{
    if ((int)a < 0 || (int)a >= 3) return;
    int32_t p = (int32_t)(mm * AxisCfg::PULSES_PER_MM + 0.5f);
    __disable_irq();
    s_axis[(int)a].pos_pulses = p;
    __enable_irq();
}

void on_timer_update(Axis a)
{
    if ((int)a < 0 || (int)a >= 3) return;
    AxisState &ax = s_axis[(int)a];
    if (!ax.busy) return;
    if (ax.pulses_left > 0) {
        --ax.pulses_left;
        ax.pos_pulses += ax.dir_positive ? 1 : -1;
        if (ax.pulses_left == 0) {
            __HAL_TIM_DISABLE_IT(&ax.htim, TIM_IT_UPDATE);
            HAL_TIM_PWM_Stop(&ax.htim, ax.channel);
            // MOE тільки advanced TIM1 (не TIM3/TIM4) - див abort() L225
            if (ax.htim.Instance == TIM1) {
                __HAL_TIM_MOE_DISABLE(&ax.htim);
            }
            ax.busy = false;
        }
    }
}

} // namespace MotorModule

extern "C" {

void TIM1_UP_TIM10_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&MotorModule::s_axis[0].htim, TIM_FLAG_UPDATE) &&
        __HAL_TIM_GET_IT_SOURCE(&MotorModule::s_axis[0].htim, TIM_IT_UPDATE)) {
        __HAL_TIM_CLEAR_FLAG(&MotorModule::s_axis[0].htim, TIM_FLAG_UPDATE);
        MotorModule::on_timer_update(Axis::X);
    }
}

void TIM3_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&MotorModule::s_axis[1].htim, TIM_FLAG_UPDATE) &&
        __HAL_TIM_GET_IT_SOURCE(&MotorModule::s_axis[1].htim, TIM_IT_UPDATE)) {
        __HAL_TIM_CLEAR_FLAG(&MotorModule::s_axis[1].htim, TIM_FLAG_UPDATE);
        MotorModule::on_timer_update(Axis::Y);
    }
}

void TIM4_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&MotorModule::s_axis[2].htim, TIM_FLAG_UPDATE) &&
        __HAL_TIM_GET_IT_SOURCE(&MotorModule::s_axis[2].htim, TIM_IT_UPDATE)) {
        __HAL_TIM_CLEAR_FLAG(&MotorModule::s_axis[2].htim, TIM_FLAG_UPDATE);
        MotorModule::on_timer_update(Axis::Z);
    }
}

} // extern "C"
