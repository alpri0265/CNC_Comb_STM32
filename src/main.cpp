#include <stm32f4xx_hal.h>
#include "Config.h"
#include "SafetyModule.h"
#include "ScaleModule.h"
#include "MotorModule.h"
#include "SpindleModule.h"
#include "StorageModule.h"
#include "DisplayModule.h"
#include "EncoderModule.h"
#include "ProgramModule.h"

extern "C" {

void SystemClock_Config(void);
void Error_Handler(void);
static void DWT_Init(void);
void HAL_DelayUs(uint32_t us);

void SysTick_Handler(void)
{
    HAL_IncTick();
}

} // extern "C"

static uint32_t s_core_clock_hz;

static void DWT_Init()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

void HAL_DelayUs(uint32_t us)
{
    uint32_t clk_per_us = s_core_clock_hz / 1000000u;
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * clk_per_us;
    while ((DWT->CYCCNT - start) < ticks) { __NOP(); }
}

static void enable_all_gpio_clocks()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
}

static void outputs_safe_state()
{
    // Drivers ENA HIGH (disabled): X PE11, Y PB8, Z PD14
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_11, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_SET);
    // Z brake applied (HIGH)
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_15, GPIO_PIN_SET);
    // Spindle EN LOW (disabled)
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET);
    // LEDs: STATUS off, FAULT on
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_SET);
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    s_core_clock_hz = HAL_RCC_GetHCLKFreq();
    DWT_Init();
    enable_all_gpio_clocks();
    outputs_safe_state();

    // Init modules in order: safety first, then motion, then HMI
    SafetyModule::init();       // sets ESTOP fault by default (until released)
    ScaleModule::init();
    MotorModule::init();        // GPIO + timers (ENA HIGH by default)
    SpindleModule::init();      // PWM = 0%, EN = LOW
    StorageModule::init();      // SDIO pins + placeholder
    DisplayModule::init();      // I2C1 + HD44780 + splash
    EncoderModule::init();      // EC11 EXTI + key
    ProgramModule::init();      // IDLE state
    ProgramModule::load_recipe(); // try to read RECIPE.TXT (else defaults)

    // Вітальний екран: SD статус + ім'я рецепту
    {
        bool sd_mount  = StorageModule::is_sd_ready();
        const char *rn = ProgramModule::has_recipe() ? ProgramModule::recipe().name : "default";
        DisplayModule::show_banner_sd_status(sd_mount, rn);
        HAL_Delay(1800); // 1.8с показати банер, потім головний екран
    }

    // Clear FAULT if ESTOP is released within the first 2 seconds
    if (!SafetyModule::estop_active()) {
        SafetyModule::clear_faults();
    }

    uint32_t last_ms     = HAL_GetTick();
    uint32_t last_disp   = HAL_GetTick();
    bool start_prev = false;
    bool stop_prev  = false;

    for (;;) {
        uint32_t now = HAL_GetTick();

        // 1 ms tasks
        if (now - last_ms >= 1) {
            last_ms = now;
            SafetyModule::tick_1ms();
            EncoderModule::tick();

            bool start_now = (HAL_GPIO_ReadPin(PinCfg::BTN_GPIOx, PinCfg::BTN_START_PIN) == GPIO_PIN_RESET);
            bool stop_now  = (HAL_GPIO_ReadPin(PinCfg::BTN_GPIOx, PinCfg::BTN_STOP_PIN)  == GPIO_PIN_RESET);
            if (start_now && !start_prev) ProgramModule::start();
            if (stop_now  && !stop_prev)  ProgramModule::stop();
            start_prev = start_now;
            stop_prev  = stop_now;

            int32_t enc_diff = EncoderModule::get_diff_and_reset();
            (void)enc_diff;

            if (EncoderModule::short_press()) {
                ProgramState st = ProgramModule::get_state();
                if (st == ProgramState::IDLE || st == ProgramState::DONE || st == ProgramState::READY)
                    ProgramModule::start();
                else if (st != ProgramState::FAULT)
                    ProgramModule::stop();
            }
            if (EncoderModule::long_press()) {
                if (ProgramModule::get_state() == ProgramState::FAULT &&
                    !SafetyModule::estop_active()) {
                    SafetyModule::clear_faults();
                    ProgramModule::init();
                }
            }
        }

        // Main state machine step
        ProgramModule::step();

        // Display refresh every 100 ms
        if (now - last_disp >= 100) {
            last_disp = now;
            float   xcmd   = MotorModule::get_position_mm(Axis::X);
            float   xscale = ScaleModule::get_position_mm();
            float   ymm    = MotorModule::get_position_mm(Axis::Y);
            float   zmm    = MotorModule::get_position_mm(Axis::Z);
            uint16_t rpm   = SpindleModule::get_rpm_setpoint();
            ProgramState st= ProgramModule::get_state();
            uint32_t idx   = ProgramModule::get_slot_index();
            uint32_t tot   = ProgramModule::has_recipe() ? ProgramModule::recipe().slots_count : 0;
            bool sd_ok     = StorageModule::is_sd_ready();
            const char *rn = ProgramModule::has_recipe() ? ProgramModule::recipe().name : "default";
            DisplayModule::show_main(xcmd, xscale, ymm, zmm, rpm, st, idx, tot, sd_ok, rn);
        }
    }
}

// ============ SystemClock_Config: HSI -> PLL -> 168 MHz ============
extern "C" void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState       = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM       = 16;   // HSI=16 MHz -> 1 MHz VCO in
    osc.PLL.PLLN       = 336;  // VCO = 336 MHz
    osc.PLL.PLLP       = RCC_PLLP_DIV2;  // SYSCLK = 168 MHz
    osc.PLL.PLLQ       = 7;    // USB OTG FS = 48 MHz (336/7)
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    RCC_ClkInitTypeDef clk = {};
    clk.ClockType      = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                          RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2);
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;     // HCLK  = 168 MHz
    clk.APB1CLKDivider = RCC_HCLK_DIV4;       // APB1  = 42 MHz  -> timers x2 = 84 MHz
    clk.APB2CLKDivider = RCC_HCLK_DIV2;       // APB2  = 84 MHz  -> timers x2 = 168 MHz
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) Error_Handler();

    HAL_RCC_EnableCSS();
}

extern "C" void Error_Handler(void)
{
    __disable_irq();
    outputs_safe_state();
    for (volatile unsigned i = 0;; i++) {
        HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_14);
        for (volatile unsigned j = 0; j < 4000000; j++) { __NOP(); }
    }
}

extern "C" void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    HAL_NVIC_SetPriority(MemoryManagement_IRQn, 0, 0);
    HAL_NVIC_SetPriority(BusFault_IRQn, 0, 0);
    HAL_NVIC_SetPriority(UsageFault_IRQn, 0, 0);
}
