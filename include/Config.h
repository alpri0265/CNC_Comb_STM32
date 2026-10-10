#pragma once
#include <stm32f4xx_hal.h>
#include <cstdint>
#include <cstdbool>
#include <cstddef>

namespace PinCfg {

// ============================================================
// TB6600 X-axis (SFU1605, HGR20/HGH20CA)
// PUL  -> PE9  TIM1_CH1 (PWM pulse generation)
// DIR  -> PE10
// ENA  -> PE11  active LOW (ENA=LOW -> dvr ON)
// ============================================================
constexpr uint16_t X_PUL_PIN  = GPIO_PIN_9;
constexpr uint16_t X_DIR_PIN  = GPIO_PIN_10;
constexpr uint16_t X_ENA_PIN  = GPIO_PIN_11;
GPIO_TypeDef * const X_GPIOx    = GPIOE;

constexpr uint16_t X_HOME_PIN  = GPIO_PIN_0;
constexpr uint16_t X_LIMP_PIN  = GPIO_PIN_1;
constexpr uint16_t X_LIMM_PIN  = GPIO_PIN_2;
GPIO_TypeDef * const X_LIM_GPIOx = GPIOF;

// ============================================================
// TB6600 Y-axis
// PUL  -> PB4  TIM3_CH1 (AF2)
// DIR  -> PB5
// ENA  -> PB8
// ============================================================
constexpr uint16_t Y_PUL_PIN  = GPIO_PIN_4;
constexpr uint16_t Y_DIR_PIN  = GPIO_PIN_5;
constexpr uint16_t Y_ENA_PIN  = GPIO_PIN_8;
GPIO_TypeDef * const Y_GPIOx    = GPIOB;

constexpr uint16_t Y_HOME_PIN  = GPIO_PIN_3;
constexpr uint16_t Y_LIMP_PIN  = GPIO_PIN_4;
constexpr uint16_t Y_LIMM_PIN  = GPIO_PIN_5;
GPIO_TypeDef * const Y_LIM_GPIOx = GPIOF;

// ============================================================
// TB6600 Z-axis (with brake)
// PUL   -> PD12  TIM4_CH1 (AF2)
// DIR   -> PD13
// ENA   -> PD14
// BRAKE -> PD15  (LOW -> brake released, HIGH -> brake applied)
// ============================================================
constexpr uint16_t Z_PUL_PIN   = GPIO_PIN_12;
constexpr uint16_t Z_DIR_PIN   = GPIO_PIN_13;
constexpr uint16_t Z_ENA_PIN   = GPIO_PIN_14;
constexpr uint16_t Z_BRAKE_PIN = GPIO_PIN_15;
GPIO_TypeDef * const Z_GPIOx     = GPIOD;

constexpr uint16_t Z_HOME_PIN  = GPIO_PIN_6;
constexpr uint16_t Z_LIMP_PIN  = GPIO_PIN_7;
constexpr uint16_t Z_LIMM_PIN  = GPIO_PIN_8;
GPIO_TypeDef * const Z_LIM_GPIOx = GPIOF;

// ============================================================
// Linear scale X (TTL 5V, 5 um/pulse, A/B quad)
// A -> PC6  TIM8_CH1 (encoder IF)
// B -> PC7  TIM8_CH2
// ============================================================
constexpr uint16_t SCALE_X_A_PIN = GPIO_PIN_6;
constexpr uint16_t SCALE_X_B_PIN = GPIO_PIN_7;
GPIO_TypeDef * const SCALE_X_GPIOx = GPIOC;

// ============================================================
// Servo SD300 spindle (0-10 V input)
// PWM  -> PB14  TIM12_CH1, Fpwm ~10 kHz -> RC(2k2 + 100 nF) -> OPAMP -> 0-10 V
// EN   -> PB15  (LOW -> spindle inverter disabled)
// ============================================================
constexpr uint16_t SD300_SPD_PIN = GPIO_PIN_14;
constexpr uint16_t SD300_EN_PIN  = GPIO_PIN_15;
GPIO_TypeDef * const SD300_GPIOx   = GPIOB;

// ============================================================
// SD-card (SDMMC1 / SDIO)
// D0  PC8, D1 PC9, D2 PC10, D3 PC11, CK PC12, CMD PD2
// ============================================================
constexpr uint16_t SD_D0_PIN = GPIO_PIN_8;
constexpr uint16_t SD_D1_PIN = GPIO_PIN_9;
constexpr uint16_t SD_D2_PIN = GPIO_PIN_10;
constexpr uint16_t SD_D3_PIN = GPIO_PIN_11;
constexpr uint16_t SD_CK_PIN = GPIO_PIN_12;
GPIO_TypeDef * const SD_D_GPIOx = GPIOC;

constexpr uint16_t SD_CMD_PIN = GPIO_PIN_2;
GPIO_TypeDef * const SD_CMD_GPIOx = GPIOD;

// ============================================================
// LCD 2004 (I2C1, 400 kHz, PCF8574 backpack)
// SCL PB6 (AF4 I2C1_SCL)
// SDA PB7 (AF4 I2C1_SDA)
// ============================================================
constexpr uint16_t LCD_SCL_PIN = GPIO_PIN_6;
constexpr uint16_t LCD_SDA_PIN = GPIO_PIN_7;
GPIO_TypeDef * const LCD_GPIOx   = GPIOB;
constexpr uint8_t  LCD_I2C_ADDR  = 0x27 << 1;
constexpr uint8_t  LCD_COLS      = 20;
constexpr uint8_t  LCD_ROWS      = 4;

// ============================================================
// Rotary encoder EC11 (menu) + keys
// EC11_A  PG10 (EXTI)
// EC11_B  PG11
// EC11_K  PG12 (key, short / long press)
// ============================================================
constexpr uint16_t EC11_A_PIN   = GPIO_PIN_10;
constexpr uint16_t EC11_B_PIN   = GPIO_PIN_11;
constexpr uint16_t EC11_KEY_PIN = GPIO_PIN_12;
GPIO_TypeDef * const EC11_GPIOx   = GPIOG;

// Cycle start / stop push buttons
constexpr uint16_t BTN_START_PIN = GPIO_PIN_1;
constexpr uint16_t BTN_STOP_PIN  = GPIO_PIN_2;
GPIO_TypeDef * const BTN_GPIOx   = GPIOG;

// Emergency stop (NC, active LOW, pull-up)
constexpr uint16_t ESTOP_PIN    = GPIO_PIN_0;
GPIO_TypeDef * const ESTOP_GPIOx = GPIOG;

// Status LEDs (active HIGH)
constexpr uint16_t LED_STATUS_PIN = GPIO_PIN_13;
constexpr uint16_t LED_FAULT_PIN  = GPIO_PIN_14;
GPIO_TypeDef * const LED_GPIOx   = GPIOG;

// ============================================================
// Debug UART (USART1)
// TX PA9, RX PA10, 115200 8N1
// ============================================================
constexpr uint16_t DBG_TX_PIN = GPIO_PIN_9;
constexpr uint16_t DBG_RX_PIN = GPIO_PIN_10;
GPIO_TypeDef * const DBG_GPIOx = GPIOA;

} // namespace PinCfg

namespace AxisCfg {

// ============================================================
// Mechanical parameters (SFU1605 ball-screw + 1.8deg hybrid)
// ============================================================
constexpr float SCREW_LEAD_MM_PER_REV = 5.0f;       // SFU1605 = 5 mm/rev
constexpr uint16_t STEPS_PER_REV      = 200;         // 1.8 deg
constexpr uint16_t MICROSTEP          = 16;          // TB6600 SW1..SW3: off-on-on (table 16)
constexpr uint32_t PULSES_PER_REV     = STEPS_PER_REV * MICROSTEP; // 3200
constexpr float PULSES_PER_MM         = (float)PULSES_PER_REV / SCREW_LEAD_MM_PER_REV; // 640.0 pulse/mm
constexpr float MM_PER_PULSE          = SCREW_LEAD_MM_PER_REV / (float)PULSES_PER_REV; // 0.0015625 mm

// Linear scale X: 5 um/pulse
constexpr float SCALE_MM_PER_PULSE    = 0.005f;       // 5 um
constexpr float SCALE_PULSES_PER_MM   = 1.0f / SCALE_MM_PER_PULSE; // 200.0

// Traverse limits
constexpr float X_MAX_MM              = 470.0f;       // safety: >= 460 + 10 mm overtravel
constexpr float Y_MAX_MM              = 100.0f;
constexpr float Z_MAX_MM              = 150.0f;

// Feedrate defaults
constexpr float DEFAULT_FEED_MM_MIN   = 60.0f;        // brass cut
constexpr float RAPID_FEED_MM_MIN     = 400.0f;       // traverse
constexpr float Z_FEED_CUT_MM_MIN     = 30.0f;
constexpr float Z_APPROACH_MM         = 3.0f;

// Timer clocks (AHB 168, APB1 42, APB1-tim x2 = 84; APB2 84, APB2-tim x2 = 168)
constexpr uint32_t APB1_TIM_CK        = 84000000;
constexpr uint32_t APB2_TIM_CK        = 168000000;

} // namespace AxisCfg

namespace SpindleCfg {
constexpr uint32_t PWM_FREQ_HZ      = 10000;          // 10 kHz PWM for 0-10 V
constexpr uint16_t PWM_MAX_COUNTS   = 1000;           // 0.1% resolution
constexpr uint16_t RPM_MAX          = 3000;           // SD300 0-10 V -> 0-3000 rpm (factory)
constexpr uint16_t RPM_TARGET       = 250;            // spec for HSS blade D80 x 1.0
} // namespace SpindleCfg

namespace SafetyCfg {
constexpr bool     ESTOP_ACTIVE_LEVEL  = false;       // NC -> LOW when pressed / line cut
constexpr bool     HOME_ACTIVE_LEVEL   = false;       // sensor: N.O. -> true when home (verify with real sensor)
constexpr bool     LIMIT_ACTIVE_LEVEL  = false;       // N.O. end-stop
constexpr uint32_t DEBOUNCE_MS         = 20;
constexpr uint16_t BUTTON_LONG_MS      = 800;
} // namespace SafetyCfg

namespace Recipe {
constexpr size_t KEY_MAX_LEN         = 32;
constexpr size_t VALUE_MAX_LEN       = 32;
constexpr size_t LINE_MAX_LEN        = 80;
constexpr const char * FILE_NAME     = "0:/RECIPE.TXT";
} // namespace Recipe

enum class Axis { X = 0, Y = 1, Z = 2 };

enum class ProgramState : uint8_t {
    IDLE,
    HOME_XYZ,
    READY,
    APPROACH_Z,
    START_SPINDLE,
    CUT_SLOT_Y,
    RETRACT_Z,
    INDEX_X,
    DONE,
    PAUSED,
    FAULT
};
