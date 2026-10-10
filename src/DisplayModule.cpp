#include "DisplayModule.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace DisplayModule {

static I2C_HandleTypeDef s_hi2c1;
static bool              s_bl = true;

static void i2c1_init()
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Pin       = PinCfg::LCD_SCL_PIN | PinCfg::LCD_SDA_PIN;
    gpio.Mode      = GPIO_MODE_AF_OD;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(PinCfg::LCD_GPIOx, &gpio);

    s_hi2c1.Instance             = I2C1;
    s_hi2c1.Init.ClockSpeed      = 400000;
    s_hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    s_hi2c1.Init.OwnAddress1     = 0;
    s_hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    s_hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_hi2c1.Init.OwnAddress2     = 0;
    s_hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&s_hi2c1);
}

// PCF8574 pinning: RS=P0, RW=P1, EN=P2, BL=P3, D4..D7 = P4..P7
static constexpr uint8_t PCF_RS = 0x01;
static constexpr uint8_t PCF_RW = 0x02;
static constexpr uint8_t PCF_EN = 0x04;
static constexpr uint8_t PCF_BL = 0x08;

static void write_byte(uint8_t val)
{
    uint8_t v = val | (s_bl ? PCF_BL : 0);
    HAL_I2C_Master_Transmit(&s_hi2c1, PinCfg::LCD_I2C_ADDR, &v, 1, 10);
}

static void pulse_enable(uint8_t data)
{
    write_byte(data);
    HAL_DelayUs(1);
    write_byte(data | PCF_EN);
    HAL_DelayUs(1);
    write_byte(data & ~PCF_EN);
    HAL_DelayUs(50);
}

static void send_nibble(uint8_t nib, bool is_cmd)
{
    uint8_t d = (uint8_t)((nib & 0x0F) << 4);
    if (!is_cmd) d |= PCF_RS;
    pulse_enable(d);
}

static void send(uint8_t val, bool is_cmd)
{
    send_nibble((uint8_t)(val >> 4), is_cmd);
    send_nibble((uint8_t)(val & 0x0F), is_cmd);
}

static void init_hd44780()
{
    HAL_Delay(50);
    // Force 8-bit mode (classic init sequence)
    send_nibble(0x03, true); HAL_Delay(5);
    send_nibble(0x03, true); HAL_Delay(1);
    send_nibble(0x03, true); HAL_Delay(1);
    // Switch to 4-bit
    send_nibble(0x02, true); HAL_Delay(2);
    // Function set: 4-bit, 4 lines, 5x8 (0x28)
    send(0x28, true); HAL_Delay(1);
    // Display off
    send(0x08, true); HAL_Delay(1);
    // Clear
    send(0x01, true); HAL_Delay(2);
    // Entry mode: cursor++
    send(0x06, true); HAL_Delay(1);
    // Display on, cursor off, blink off
    send(0x0C, true); HAL_Delay(1);
}

void init()
{
    i2c1_init();
    init_hd44780();
}

void clear() { send(0x01, true); HAL_Delay(2); }
void home()  { send(0x02, true); HAL_Delay(2); }

void set_cursor(uint8_t col, uint8_t row)
{
    static const uint8_t row_ofs[] = { 0x00, 0x40, 0x14, 0x54 };
    if (row >= 4) row = 3;
    if (col >= PinCfg::LCD_COLS) col = PinCfg::LCD_COLS - 1;
    send((uint8_t)(0x80 | (row_ofs[row] + col)), true);
}

void print(const char *s)
{
    if (!s) return;
    while (*s) send((uint8_t)*s++, false);
}

void printf(const char *fmt, ...)
{
    char buf[PinCfg::LCD_COLS + 1];
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    print(buf);
}

void backlight(bool on)
{
    s_bl = on;
    write_byte(s_bl ? PCF_BL : 0);
}

static const char *state_str(ProgramState s)
{
    switch (s) {
        case ProgramState::IDLE:       return "IDLE  ";
        case ProgramState::HOME_XYZ:   return "HOME  ";
        case ProgramState::READY:      return "READY ";
        case ProgramState::APPROACH_Z: return "APPR  ";
        case ProgramState::START_SPINDLE: return "SPIN  ";
        case ProgramState::CUT_SLOT_Y: return "CUT   ";
        case ProgramState::RETRACT_Z:  return "RET   ";
        case ProgramState::INDEX_X:    return "INDEX ";
        case ProgramState::DONE:       return "DONE  ";
        case ProgramState::PAUSED:     return "PAUSE ";
        case ProgramState::FAULT:      return "FAULT ";
    }
    return "?     ";
}

void show_main(float x_cmd, float x_scale, float y_mm, float z_mm,
               uint16_t rpm, ProgramState st, uint32_t idx, uint32_t tot)
{
    set_cursor(0, 0);
    printf("Xc%7.3f Xs%7.3f", x_cmd, x_scale);
    set_cursor(0, 1);
    printf("Y%7.3f Z%8.3f", y_mm, z_mm);
    set_cursor(0, 2);
    printf("RPM %4u %s", (unsigned)rpm, state_str(st));
    set_cursor(0, 3);
    printf("Slot %3u/%3u        ", (unsigned)(idx > tot ? tot : idx), (unsigned)tot);
}

} // namespace DisplayModule
