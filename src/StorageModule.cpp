#include "StorageModule.h"
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace StorageModule {

static bool s_sd_ready = false;

void init()
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SDIO_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_SDIO;
    gpio.Pin       = PinCfg::SD_D0_PIN | PinCfg::SD_D1_PIN |
                      PinCfg::SD_D2_PIN | PinCfg::SD_D3_PIN | PinCfg::SD_CK_PIN;
    HAL_GPIO_Init(PinCfg::SD_D_GPIOx, &gpio);
    gpio.Pin = PinCfg::SD_CMD_PIN;
    HAL_GPIO_Init(PinCfg::SD_CMD_GPIOx, &gpio);

    // Каркас: фактично HAL_SD_Init / FatFS mounting буде реалізовано окремо після інтеграції BSP SD.
    // Для збірки залишаємо як завантажену-зразок (пізніше підключити stm32f4xx_hal_sd.c).
    s_sd_ready = false;
}

bool is_sd_ready() { return s_sd_ready; }

void set_defaults(RecipeData &r)
{
    std::memset(&r, 0, sizeof(r));
    std::strncpy(r.name, "comb_2mm_default", Recipe::VALUE_MAX_LEN - 1);
    r.pass_count    = 225;
    r.pitch_mm      = 2.000f;
    r.slot_width_mm = 1.0f;
    r.slot_depth_mm = 12.0f;
    r.spindle_rpm   = SpindleCfg::RPM_TARGET;
    r.feed_mm_min   = AxisCfg::DEFAULT_FEED_MM_MIN;
    r.z_approach_mm = AxisCfg::Z_APPROACH_MM;
}

bool parse_line(const char *line, char *key, char *value)
{
    if (!line) return false;
    while (*line && std::isspace((unsigned char)*line)) ++line;
    if (*line == 0 || *line == '#' || *line == ';') return false;

    size_t i = 0;
    while (*line && *line != '=' && !std::isspace((unsigned char)*line) && i < Recipe::KEY_MAX_LEN - 1) {
        key[i++] = *line++;
    }
    key[i] = 0;
    while (*line && (std::isspace((unsigned char)*line) || *line == '=')) ++line;

    i = 0;
    while (*line && *line != '\r' && *line != '\n' && *line != '#' && *line != ';' &&
           i < Recipe::VALUE_MAX_LEN - 1) {
        value[i++] = *line++;
    }
    while (i > 0 && std::isspace((unsigned char)value[i - 1])) --i;
    value[i] = 0;

    return (key[0] != 0);
}

static bool ieq(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
        ++a; ++b;
    }
    return (*a == 0 && *b == 0);
}

void apply_kv(RecipeData &r, const char *key, const char *value)
{
    if (ieq(key, "RECIPE_NAME"))   { std::strncpy(r.name, value, Recipe::VALUE_MAX_LEN - 1); return; }
    if (ieq(key, "PASS_COUNT"))    { r.pass_count    = (uint32_t)std::strtoul(value, nullptr, 10); return; }
    if (ieq(key, "PITCH_MM"))      { r.pitch_mm      = std::strtof(value, nullptr); return; }
    if (ieq(key, "SLOT_WIDTH_MM")) { r.slot_width_mm = std::strtof(value, nullptr); return; }
    if (ieq(key, "SLOT_DEPTH_MM")) { r.slot_depth_mm = std::strtof(value, nullptr); return; }
    if (ieq(key, "SPINDLE_RPM"))   { r.spindle_rpm   = (uint16_t)std::strtoul(value, nullptr, 10); return; }
    if (ieq(key, "FEED_MM_MIN"))   { r.feed_mm_min   = std::strtof(value, nullptr); return; }
    if (ieq(key, "Z_APPROACH_MM")) { r.z_approach_mm = std::strtof(value, nullptr); return; }
}

bool read_recipe(RecipeData &out)
{
    set_defaults(out);
    if (!s_sd_ready) return false;

    // TODO (BSP/FatFs integration):
    // 1) f_open(&fil, Recipe::FILE_NAME, FA_READ)
    // 2) f_gets(line, LINE_MAX_LEN, &fil) in a loop
    // 3) parse_line -> apply_kv
    // 4) f_close
    // Поки повертаємо false (тільки defaults).
    return false;
}

} // namespace StorageModule
