#include "ScaleModule.h"

namespace ScaleModule {

static TIM_HandleTypeDef s_htim8;
static volatile int32_t  s_rollover;

void init()
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_TIM8_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Pin       = PinCfg::SCALE_X_A_PIN | PinCfg::SCALE_X_B_PIN;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF3_TIM8;
    HAL_GPIO_Init(PinCfg::SCALE_X_GPIOx, &gpio);

    s_htim8.Instance               = TIM8;
    s_htim8.Init.Prescaler         = 0;
    s_htim8.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_htim8.Init.Period            = 0xFFFF;
    s_htim8.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_htim8.Init.RepetitionCounter = 0;
    s_htim8.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    TIM_Encoder_InitTypeDef enc = {};
    enc.EncoderMode  = TIM_ENCODERMODE_TI12;
    enc.IC1Polarity  = TIM_ICPOLARITY_RISING;
    enc.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC1Prescaler = TIM_ICPSC_DIV1;
    enc.IC1Filter    = 10;
    enc.IC2Polarity  = TIM_ICPOLARITY_RISING;
    enc.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC2Prescaler = TIM_ICPSC_DIV1;
    enc.IC2Filter    = 10;
    HAL_TIM_Encoder_Init(&s_htim8, &enc);

    TIM_SlaveConfigTypeDef sc = {};
    sc.SlaveMode = TIM_SLAVEMODE_DISABLE;
    sc.InputTrigger = TIM_TS_ITR0;
    HAL_TIM_SlaveConfigSynchro(&s_htim8, &sc);

    __HAL_TIM_CLEAR_FLAG(&s_htim8, TIM_FLAG_UPDATE);
    __HAL_TIM_ENABLE_IT(&s_htim8, TIM_IT_UPDATE);

    s_rollover = 0;
    __HAL_TIM_SET_COUNTER(&s_htim8, 0);
    HAL_TIM_Encoder_Start(&s_htim8, TIM_CHANNEL_ALL);

    HAL_NVIC_SetPriority(TIM8_UP_TIM13_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM8_UP_TIM13_IRQn);
}

int32_t get_raw_count()
{
    __disable_irq();
    uint16_t cnt = (uint16_t)__HAL_TIM_GET_COUNTER(&s_htim8);
    int32_t  rol = s_rollover;
    uint32_t sr  = s_htim8.Instance->SR;
    __enable_irq();

    int32_t combined = rol + cnt;

    if (sr & TIM_FLAG_UPDATE) {
        // missed, safe: treat 0/0xFFFF boundaries conservatively
        if (cnt < 0x8000) combined += 0x10000;
    }
    return combined;
}

float get_position_mm()
{
    return (float)get_raw_count() * AxisCfg::SCALE_MM_PER_PULSE;
}

void set_zero()
{
    __disable_irq();
    s_rollover = 0;
    __HAL_TIM_SET_COUNTER(&s_htim8, 0);
    __HAL_TIM_CLEAR_FLAG(&s_htim8, TIM_FLAG_UPDATE);
    __enable_irq();
}

void set_position_mm(float mm)
{
    int32_t cnt_target = (int32_t)(mm * AxisCfg::SCALE_PULSES_PER_MM + 0.5f);
    __disable_irq();
    s_rollover = cnt_target & 0xFFFF0000;
    __HAL_TIM_SET_COUNTER(&s_htim8, (uint32_t)(cnt_target & 0xFFFF));
    __HAL_TIM_CLEAR_FLAG(&s_htim8, TIM_FLAG_UPDATE);
    __enable_irq();
}

void on_update_isr()
{
    if (__HAL_TIM_GET_FLAG(&s_htim8, TIM_FLAG_UPDATE)) {
        __HAL_TIM_CLEAR_FLAG(&s_htim8, TIM_FLAG_UPDATE);
        uint16_t cnt = (uint16_t)__HAL_TIM_GET_COUNTER(&s_htim8);
        if (cnt < 0x8000) s_rollover += 0x10000;
        else              s_rollover -= 0x10000;
    }
}

} // namespace ScaleModule

extern "C" void TIM8_UP_TIM13_IRQHandler(void)
{
    ScaleModule::on_update_isr();
}
