#include "SpindleModule.h"

namespace SpindleModule {

static TIM_HandleTypeDef s_htim12;
static uint16_t          s_rpm_setpoint;
static bool              s_enabled;

void init()
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM12_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Pin       = PinCfg::SD300_SPD_PIN;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF9_TIM12;
    HAL_GPIO_Init(PinCfg::SD300_GPIOx, &gpio);

    gpio.Pin  = PinCfg::SD300_EN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(PinCfg::SD300_GPIOx, &gpio);
    HAL_GPIO_WritePin(PinCfg::SD300_GPIOx, PinCfg::SD300_EN_PIN, GPIO_PIN_RESET); // safe disabled

    // APB1 timer: 84 MHz (x2 multiplier applied) -> CK_INT = 84 MHz
    // Target: Fpwm = ~10 kHz, ARR = 999 -> PSC = 84000000/(999+1)/10000 - 1 = 7
    s_htim12.Instance               = TIM12;
    s_htim12.Init.Prescaler         = 7;
    s_htim12.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_htim12.Init.Period            = 999;
    s_htim12.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_htim12.Init.RepetitionCounter = 0;
    s_htim12.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&s_htim12);

    TIM_OC_InitTypeDef oc = {};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&s_htim12, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&s_htim12, TIM_CHANNEL_1);

    s_rpm_setpoint = 0;
    s_enabled      = false;
}

void set_rpm(uint16_t rpm)
{
    if (rpm > SpindleCfg::RPM_MAX) rpm = SpindleCfg::RPM_MAX;
    s_rpm_setpoint = rpm;
    uint16_t ccr = (uint16_t)((uint32_t)rpm * 999u / (uint32_t)SpindleCfg::RPM_MAX);
    if (ccr > 999) ccr = 999;
    __HAL_TIM_SET_COMPARE(&s_htim12, TIM_CHANNEL_1, ccr);
}

uint16_t get_rpm_setpoint() { return s_rpm_setpoint; }

void enable(bool on)
{
    s_enabled = on;
    HAL_GPIO_WritePin(PinCfg::SD300_GPIOx, PinCfg::SD300_EN_PIN,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool is_enabled() { return s_enabled; }

void fault_stop()
{
    set_rpm(0);
    enable(false);
}

} // namespace SpindleModule
