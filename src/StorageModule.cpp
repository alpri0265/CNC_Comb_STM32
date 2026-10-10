#include "StorageModule.h"
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>

// ====================================================================
// FatFS integration: додайте в platformio.ini build_flags = -DUSE_FATFS
// і підключіть BSP FatFS + SDIO diskio driver (stm32f4xx_hal_sd.c,
// fatfs.c, ff.c, ff_gen_drv.c, sd_diskio.c).
// Поки define НЕ включений — працює fallback на defaults, SD недоступна.
// ====================================================================
#ifdef USE_FATFS
    #include "ff.h"
    static FATFS s_fatfs;
    static bool  s_fatfs_mounted = false;
#endif

namespace StorageModule {

static bool s_sd_ready = false;

void init()
{
    // RCC + GPIO SDIO (PC8..PC12 D0..CK, PD2 CMD AF12)
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

#ifdef USE_FATFS
    // Спроба змонтувати том 0:
    std::memset(&s_fatfs, 0, sizeof(s_fatfs));
    FRESULT fr = f_mount(&s_fatfs, "0:", 1 /*force mount now*/);
    s_fatfs_mounted = (fr == FR_OK);
    s_sd_ready      = s_fatfs_mounted;
    // Якщо монтування не відбулося (нема карти, або не FAT32) → s_sd_ready=false,
    // read_recipe() поверне defaults. Повторний mount можна зробити в loop
    // по кнопці (або робити f_mount кожен раз перед читанням — дешевше).
#else
    s_sd_ready = false;
#endif
}

bool is_sd_ready() { return s_sd_ready; }

void set_defaults(RecipeData &r)
{
    std::memset(&r, 0, sizeof(r));
    std::strncpy(r.name, "comb_2mm_default", Recipe::VALUE_MAX_LEN - 1);
    r.slots_count          = 225;
    r.pass_count           = r.slots_count;  // alias, синхронізовано
    r.pitch_mm             = AxisCfg::SLOT_STEP_MM;          // 2.000
    r.slot_width_mm        = AxisCfg::SLOT_WIDTH_MM;         // 1.0
    r.slot_depth_mm        = AxisCfg::SLOT_DEPTH_MM;         // 12.0
    r.spindle_rpm          = SpindleCfg::RPM_TARGET;         // 250
    r.feed_mm_min          = AxisCfg::DEFAULT_FEED_MM_MIN;   // 60
    r.z_approach_mm        = AxisCfg::Z_APPROACH_MM;         // 3.0
    r.rapid_mm_min         = AxisCfg::RAPID_FEED_MM_MIN;     // 400
    r.material_thickness_mm= AxisCfg::MATERIAL_THICKNESS_MM; // 3.0
    r.slot_overcut_mm      = AxisCfg::SLOT_OVERCUT_MM;       // 0.3
    r.z_feed_plunge_mm_min = AxisCfg::Z_FEED_PLUNGE_MM_MIN;  // 15
    r.z_feed_cut_mm_min    = AxisCfg::Z_FEED_CUT_MM_MIN;     // 30
    r.backlash_x_mm        = AxisCfg::BACKLASH_X_MM;         // 0.020
    r.corr_tol_mm          = AxisCfg::CORR_TOL_MM;           // 0.010
    r.feed_corr_mm_min     = AxisCfg::FEED_CORR_MM_MIN;      // 100
}

bool parse_line(const char *line, char *key, char *value)
{
    if (!line || !key || !value) return false;
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

// ---------- Range validation helpers ----------
static uint32_t clamp_u32(uint32_t x, uint32_t lo, uint32_t hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}
static float clamp_f(float x, float lo, float hi) {
    if (std::isnan(x)) return lo;
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

void apply_kv(RecipeData &r, const char *key, const char *value, bool *has_changes)
{
    if (!key || !value) return;
    // Макрос-помічник: записати значення, і якщо змінено — *has_changes = true (якщо вказівник не nullptr).
    #define SET_CHANGED do { if (has_changes) *has_changes = true; } while (0)

    // ====== strings ======
    if (ieq(key, "RECIPE_NAME") || ieq(key, "NAME")) {
        std::strncpy(r.name, value, Recipe::VALUE_MAX_LEN - 1);
        r.name[Recipe::VALUE_MAX_LEN - 1] = 0;
        SET_CHANGED;
        return;
    }

    // ====== uint32 / uint16 (цілі) ======
    uint32_t u = std::strtoul(value, nullptr, 10);

    if (ieq(key, "SLOTS_COUNT") || ieq(key, "PASS_COUNT")) {
        uint16_t val = (uint16_t)clamp_u32(u, 1, 500);
        r.slots_count = val;
        r.pass_count  = val; // alias синхронізація
        SET_CHANGED;
        return;
    }
    if (ieq(key, "SPINDLE_RPM")) {
        r.spindle_rpm = (uint16_t)clamp_u32(u, SpindleCfg::RPM_MIN, SpindleCfg::RPM_MAX);
        SET_CHANGED;
        return;
    }

    // ====== floats ======
    char *end = nullptr;
    float f  = std::strtof(value, &end);
    (void)end;

    if (ieq(key, "PITCH_MM") || ieq(key, "SLOT_STEP_MM")) {
        r.pitch_mm = clamp_f(f, 0.100f, 10.000f);
        SET_CHANGED;
        return;
    }
    if (ieq(key, "SLOT_WIDTH_MM")) {
        r.slot_width_mm = clamp_f(f, 0.1f, 10.0f);
        SET_CHANGED;
        return;
    }
    if (ieq(key, "SLOT_DEPTH_MM")) {
        r.slot_depth_mm = clamp_f(f, 0.1f, AxisCfg::Y_MAX_MM);
        SET_CHANGED;
        return;
    }
    if (ieq(key, "FEED_MM_MIN") || ieq(key, "Y_FEED_MM_MIN")) {
        r.feed_mm_min = clamp_f(f, 1.0f, 2000.0f);
        SET_CHANGED;
        return;
    }
    if (ieq(key, "RAPID_MM_MIN") || ieq(key, "RAPID_FEED_MM_MIN")) {
        r.rapid_mm_min = clamp_f(f, 10.0f, 10000.0f);
        SET_CHANGED;
        return;
    }
    if (ieq(key, "Z_APPROACH_MM")) {
        r.z_approach_mm = clamp_f(f, 0.0f, AxisCfg::Z_MAX_MM);
        SET_CHANGED;
        return;
    }
    if (ieq(key, "MATERIAL_THICKNESS_MM") || ieq(key, "MAT_T_MM")) {
        r.material_thickness_mm = clamp_f(f, 0.10f, 20.0f);
        SET_CHANGED;
        return;
    }
    if (ieq(key, "SLOT_OVERCUT_MM") || ieq(key, "OVERCUT_MM")) {
        r.slot_overcut_mm = clamp_f(f, 0.0f, 5.0f);
        SET_CHANGED;
        return;
    }
    if (ieq(key, "Z_FEED_PLUNGE_MM_MIN") || ieq(key, "PLUNGE_FEED")) {
        r.z_feed_plunge_mm_min = clamp_f(f, 1.0f, 500.0f);
        SET_CHANGED;
        return;
    }
    if (ieq(key, "Z_FEED_CUT_MM_MIN") || ieq(key, "Z_FEED")) {
        r.z_feed_cut_mm_min = clamp_f(f, 1.0f, 1000.0f);
        SET_CHANGED;
        return;
    }
    if (ieq(key, "BACKLASH_X_MM")) {
        r.backlash_x_mm = clamp_f(f, 0.0f, 0.500f);
        SET_CHANGED;
        return;
    }
    if (ieq(key, "CORR_TOL_MM")) {
        r.corr_tol_mm = clamp_f(f, 0.000f, 0.100f);
        SET_CHANGED;
        return;
    }
    if (ieq(key, "FEED_CORR_MM_MIN") || ieq(key, "CORR_FEED")) {
        r.feed_corr_mm_min = clamp_f(f, 1.0f, 1000.0f);
        SET_CHANGED;
        return;
    }
    #undef SET_CHANGED
    // Невідомий ключ — ігноруємо (не помилка, просто пропускаємо).
}

// ========================== read_recipe ==========================
// Fallback: якщо не може читати (SD відсутня, файлу немає, парсинг
// зламався) — *out заповнюється defaults і повертається false.
//
// Формат RECIPE.TXT (приклад на SD):
//   ; Recipe for comb 450 mm latun
//   RECIPE_NAME = comb_latun_2mm
//   SLOTS_COUNT = 225
//   PITCH_MM    = 2.000
//   SLOT_DEPTH_MM = 12.0
//   MATERIAL_THICKNESS_MM = 3.0
//   SLOT_OVERCUT_MM = 0.3
//   SPINDLE_RPM = 250
//   FEED_MM_MIN = 60
//   RAPID_MM_MIN = 400
//   PLUNGE_FEED  = 15
//
bool read_recipe(RecipeData &out)
{
    set_defaults(out);

#ifdef USE_FATFS
    // 1) Перевірка: якщо монтування не було (картка вийшла під час роботи) —
    //    пробуємо змонтувати ще раз (f_mount з флагом 1).
    if (!s_fatfs_mounted) {
        FRESULT fr = f_mount(&s_fatfs, "0:", 1);
        s_fatfs_mounted = (fr == FR_OK);
    }
    if (!s_fatfs_mounted) return false;

    FIL fil;
    FRESULT fr = f_open(&fil, Recipe::FILE_NAME, FA_READ);
    if (fr != FR_OK) {
        // Файла немає → працюємо з defaults.
        return false;
    }

    char line[Recipe::LINE_MAX_LEN];
    char key[Recipe::KEY_MAX_LEN];
    char val[Recipe::VALUE_MAX_LEN];

    while (f_gets(line, sizeof(line), &fil) != nullptr) {
        if (!parse_line(line, key, val)) continue;
        apply_kv(out, key, val);
    }
    f_close(&fil);
    return true;
#else
    // Без USE_FATFS — нічого не робимо, вже заповнено defaults.
    (void)out;
    return false;
#endif
}

} // namespace StorageModule
