#include "EncoderModule.h"

namespace EncoderModule {

static volatile int32_t s_diff;
static volatile uint8_t s_state_a_b;

static uint32_t s_key_ms;
static uint8_t  s_key_state; // 0=up, 1=down wait debounce, 2=down stable, 3=long reported
static bool     s_short_pend;
static bool     s_long_pend;

static uint8_t read_pins()
{
    uint8_t a = (HAL_GPIO_ReadPin(PinCfg::EC11_GPIOx, PinCfg::EC11_A_PIN) == GPIO_PIN_SET) ? 1 : 0;
    uint8_t b = (HAL_GPIO_ReadPin(PinCfg::EC11_GPIOx, PinCfg::EC11_B_PIN) == GPIO_PIN_SET) ? 2 : 0;
    return (uint8_t)(a | b);
}

void init()
{
    __HAL_RCC_GPIOG_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Mode  = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin   = PinCfg::EC11_A_PIN | PinCfg::EC11_B_PIN;
    HAL_GPIO_Init(PinCfg::EC11_GPIOx, &gpio);

    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pin  = PinCfg::EC11_KEY_PIN;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(PinCfg::EC11_GPIOx, &gpio);

    s_diff       = 0;
    s_state_a_b  = read_pins();
    s_key_ms     = 0;
    s_key_state  = 0;
    s_short_pend = false;
    s_long_pend  = false;

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

int32_t get_diff_and_reset()
{
    __disable_irq();
    int32_t v = s_diff;
    s_diff = 0;
    __enable_irq();
    return v;
}

bool short_press()
{
    bool v = s_short_pend;
    s_short_pend = false;
    return v;
}

bool long_press()
{
    bool v = s_long_pend;
    s_long_pend = false;
    return v;
}

static void decode_quad()
{
    static const int8_t lut[16] = {
        0,  1, -1,  0,
       -1,  0,  0,  1,
        1,  0,  0, -1,
        0, -1,  1,  0
    };
    uint8_t now = read_pins();
    uint8_t idx = (uint8_t)((s_state_a_b << 2) | now);
    s_diff += lut[idx];
    s_state_a_b = now;
}

void on_exti_a() { decode_quad(); }
void on_exti_b() { decode_quad(); }

void tick() // 1 ms
{
    bool pressed = (HAL_GPIO_ReadPin(PinCfg::EC11_GPIOx, PinCfg::EC11_KEY_PIN) == GPIO_PIN_RESET);

    switch (s_key_state) {
        case 0: // up
            if (pressed) { s_key_ms = 0; s_key_state = 1; }
            break;
        case 1: // wait debounce
            s_key_ms++;
            if (s_key_ms >= SafetyCfg::DEBOUNCE_MS) {
                s_key_ms    = 0;
                s_key_state = pressed ? 2 : 0;
            }
            break;
        case 2: // down stable, wait long press
            s_key_ms++;
            if (!pressed) {
                s_short_pend = true;
                s_key_state  = 0;
                s_key_ms     = 0;
            } else if (s_key_ms >= SafetyCfg::BUTTON_LONG_MS) {
                s_long_pend  = true;
                s_key_state  = 3;
                s_key_ms     = 0;
            }
            break;
        case 3: // long reported; wait release
            if (!pressed) { s_key_state = 0; s_key_ms = 0; }
            break;
    }
}

} // namespace EncoderModule

extern "C" void EXTI15_10_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(PinCfg::EC11_A_PIN)) {
        __HAL_GPIO_EXTI_CLEAR_IT(PinCfg::EC11_A_PIN);
        EncoderModule::on_exti_a();
    }
    if (__HAL_GPIO_EXTI_GET_IT(PinCfg::EC11_B_PIN)) {
        __HAL_GPIO_EXTI_CLEAR_IT(PinCfg::EC11_B_PIN);
        EncoderModule::on_exti_b();
    }
    if (__HAL_GPIO_EXTI_GET_IT(PinCfg::EC11_KEY_PIN)) {
        __HAL_GPIO_EXTI_CLEAR_IT(PinCfg::EC11_KEY_PIN);
    }
}
