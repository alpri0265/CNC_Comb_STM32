---
name: "sd-cnc-recipe"
description: "STM32 CNC SD Card recipe: USE_FATFS conditional f_read + KEY=VALUE parser with 15 clamped fields + LCD integration + fallback defaults. Invoke when user asks to add SD recipe / change params without reflash / f_mount f_gets FatFS."
---

# SD-CNC-Recipe (STM32F407ZET6 CNC Comb)

Реалізація читання рецепту з SD-картки для CNC проєктів на STM32 HAL super-loop.
Ціль: міняти 15+ параметрів (225 слотів, feeds, materials, backlash, RPM) **без перепрошивки** через `RECIPE.TXT`.
Ключовий дизайн: **DEFENSIVE**: SD відсутня / файлу немає / синтаксис зламаний → **fallback на значення з Config.h** — програма працює без будь-яких змін. Без define `USE_FATFS` збірка не потребує FatFS BSP файлів.

---

## 1. Коли викликати

- Користувач просить "SD рецепт", "читати RECIPE.TXT", `f_read` + парсер `KEY=VALUE`
- Треба міняти параметри без перепрошивки (кількість слотів, feeds, RPM, товщина матеріалу тощо)
- Потрібна інтеграція SD-статусу на LCD 20x4
- Проєкт STM32 + HAL + super-loop + PlatformIO (board: `black_f407ze` / genericSTM32F407ZET6)

## 2. Принципи (НЕ ЗЛАМАТИ)

1. **USE_FATFS conditional compilation**: Весь FatFS API (`ff.h`, `f_mount`, `f_open`, `f_gets`, `f_close`, `f_mount(0)`) обгортається `#ifdef USE_FATFS`. Без define — ніяких посилань на FatFS → ніяких linker errors без BSP файлів.
2. **НЕБЛОКУЮЧІСТЬ**: Жодних `while (f_stat == FR_NOT_READY) {}`. Один виклик `f_*` з результатом → якщо не FR_OK — fallback defaults.
3. **Safe-state НЕ ЧІПАТИ**: ENA HIGH, Z BRAKE HIGH, SP EN LOW, LED FAULT ON — залишаються незмінними.
4. **Валідація перша**: ВСІ значення з файлу проходять `clamp()` до механічних діапазонів. Некоректне/відсутнє — default.
5. **Зворотна сумісність**: Старе ім'я `pass_count` (вже використане в ProgramModule) = alias до нового `slots_count`. Синхронізуються в `set_defaults` та в `apply_kv`.

---

## 3. Рецепт (RecipeData) — 15 полів + 2 інфополя

Розмістити в `include/StorageModule.h`:

| Поле | Тип | Ключ(і) RECIPE.TXT | Діапазон clamp | Default (Config.h AxisCfg::) |
|---|---|---|---|---|
| `name[32]` | char[] | RECIPE_NAME / NAME | (стрічка, макс 31) | "default" |
| `slots_count` | uint16_t | SLOTS_COUNT / PASS_COUNT | 1 .. 500 | 225 |
| `pass_count` | uint16_t (alias) | ↑ | ↑ | ↑ (synced) |
| `spindle_rpm` | uint16_t | SPINDLE_RPM | RPM_MIN .. RPM_MAX | 250 |
| `pitch_mm` | float | PITCH_MM / SLOT_STEP_MM | 0.100 .. 10.000 | 2.000 |
| `slot_width_mm` | float | SLOT_WIDTH_MM | 0.1 .. 10.0 | 1.0 |
| `slot_depth_mm` | float | SLOT_DEPTH_MM | 0.1 .. Y_MAX_MM | 12.0 |
| `feed_mm_min` | float | FEED_MM_MIN / Y_FEED_MM_MIN | 1 .. 2000 | 60 |
| `rapid_mm_min` | float | RAPID_MM_MIN / RAPID_FEED_MM_MIN | 10 .. 10000 | 400 |
| `z_approach_mm` | float | Z_APPROACH_MM | 0 .. Z_MAX_MM | 3.0 |
| `material_thickness_mm` | float | MATERIAL_THICKNESS_MM / MAT_T_MM | 0.10 .. 20.0 | 3.0 |
| `slot_overcut_mm` | float | SLOT_OVERCUT_MM / OVERCUT_MM | 0 .. 5.0 | 0.3 |
| `z_feed_plunge_mm_min` | float | Z_FEED_PLUNGE_MM_MIN / PLUNGE_FEED | 1 .. 500 | 15 |
| `z_feed_cut_mm_min` | float | Z_FEED_CUT_MM_MIN / Z_FEED | 1 .. 1000 | 30 |
| `backlash_x_mm` | float | BACKLASH_X_MM | 0 .. 0.500 | 0.020 |
| `corr_tol_mm` | float | CORR_TOL_MM | 0 .. 0.100 | 0.010 |
| `feed_corr_mm_min` | float | FEED_CORR_MM_MIN / CORR_FEED | 1 .. 1000 | 100 |

Сталі ліміти в StorageModule: `LINE_MAX_LEN=80`, `KEY_MAX_LEN=32`, `VALUE_MAX_LEN=32`. Шлях: `"0:/RECIPE.TXT"` (корінь FAT32, латиниця).

API сигнатури StorageModule:
```cpp
struct RecipeLimits {
    static constexpr size_t LINE_MAX_LEN  = 80;
    static constexpr size_t KEY_MAX_LEN   = 32;
    static constexpr size_t VALUE_MAX_LEN = 32;
    static const char*    FILE_NAME;      // "0:/RECIPE.TXT"
};
using Recipe = RecipeLimits; // alias

void set_defaults(RecipeData &r);                                   // заповнити ВСІ поля з Config.h, синхр slots_count ↔ pass_count
void apply_kv(RecipeData &r, const char *key, const char *value, bool *has_changes); // 1 ключ = clamp + запис
bool read_recipe(RecipeData &out, bool *has_sd_file = nullptr);     // якщо USE_FATFS → f_* + парсер; else → defaults
bool is_sd_ready();                                                 // true = SD монтована і файл прочитано
```

Файл Config.h має доповнитись відсутніми дефолтами (якщо нема):
```cpp
namespace AxisCfg {
  constexpr uint16_t SLOTS_COUNT   = 225;
  constexpr float    SLOT_STEP_MM  = 2.000f;
  constexpr float    SLOT_WIDTH_MM = 1.0f;
  constexpr float    SLOT_DEPTH_MM = 12.0f;
}
namespace SpindleCfg {
  constexpr uint16_t RPM_MIN = 10; // захист від ділення на 0 в clamp
}
```

---

## 4. StorageModule.cpp — деталі реалізації

### 4.1 apply_kv — макрос SET_CHANGED

Уникай `-Wmisleading-indentation` warnings. Паттерн заборонено:
```cpp
if (touched) *has_changes = true; return;  // GCC парсить як { if; } return;  == return ЗАВЖДИ + warning
```
**Коректний варіант — макрос**:
```cpp
void apply_kv(RecipeData &r, const char *key, const char *value, bool *has_changes) {
    if (!key || !value) return;
    #define SET_CHANGED do { if (has_changes) *has_changes = true; } while (0)

    uint32_t u = std::strtoul(value, nullptr, 10);
    if (ieq(key, "SLOTS_COUNT") || ieq(key, "PASS_COUNT")) {
        uint16_t val = (uint16_t)clamp_u32(u, 1, 500);
        r.slots_count = val;  r.pass_count = val;   // alias sync
        SET_CHANGED; return;
    }
    // ... решта ключів згідно таблиці §3: uint16 (SPINDLE_RPM), потім float всі via clamp_f
    char *end = nullptr;
    float f = std::strtof(value, &end);
    (void)end; // прибрати -Wunused-variable

    #undef SET_CHANGED
    // Невідомий ключ — ігноруємо (не помилка)
}
```

Додай допоміжні:
```cpp
static bool ieq(const char *a, const char *b) {      // case-insensitive ==
    while (*a && *b) {
        if (std::tolower((uint8_t)*a) != std::tolower((uint8_t)*b)) return false;
        ++a; ++b;
    }
    return !*a && !*b;
}
static void trim(char *s) {   // обрізати пробіли/таби зліва+справа
    char *p = s; while (*p==' '||*p=='\t') ++p;
    if (p!=s) std::memmove(s, p, std::strlen(p)+1);
    size_t n = std::strlen(s);
    while (n && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'||s[n-1]=='\n')) s[--n]=0;
}
static uint32_t clamp_u32(uint32_t x, uint32_t lo, uint32_t hi) { return x<lo?lo:(x>hi?hi:x); }
static float    clamp_f(float x, float lo, float hi) { return x<lo?lo:(x>hi?hi:x); }
```

### 4.2 read_recipe — f_gets line-by-line парсер

```cpp
static bool s_sd_ready = false; // глобальне в cpp, доступ через is_sd_ready()

bool read_recipe(RecipeData &out, bool *has_sd_file) {
    if (has_sd_file) *has_sd_file = false;
    set_defaults(out);             // спочатку defaults — якщо все зламається, вже валідно

#ifdef USE_FATFS
    FATFS fs;
    FRESULT fr;
    FIL fil;
    char line[Recipe::LINE_MAX_LEN];

    fr = f_mount(&fs, "", 1);       // 1 = mount now, не блокуюче — негайно повертає FR_NOT_READY
    if (fr != FR_OK) {
        f_mount(nullptr, "", 1); s_sd_ready = false;
        return false;
    }
    fr = f_open(&fil, Recipe::FILE_NAME, FA_READ);
    if (fr != FR_OK) {
        f_mount(nullptr, "", 1); s_sd_ready = false;
        return false;
    }
    bool parsed = false;
    while (f_gets(line, sizeof(line), &fil)) {
        trim(line);
        size_t n = std::strlen(line);
        if (n == 0)                               continue;
        if (line[0]=='#' || line[0]==';')         continue;   // коментарі

        char *eq = std::strchr(line, '=');
        if (!eq)                                  continue;   // не формат KEY=VALUE
        *eq = 0;
        char *k = line;     trim(k);
        char *v = eq + 1;   trim(v);
        if (!*k) continue;

        apply_kv(out, k, v, nullptr);
        parsed = true;           // хоч щось розпарсили — файл не пустий
    }
    f_close(&fil);
    f_mount(nullptr, "", 1);     // розмонтувати після читання (дозволяє витягти карту)
    s_sd_ready = parsed;
    if (has_sd_file) *has_sd_file = parsed;
    return parsed;
#else
    s_sd_ready = false;
    return false;
#endif
}
```

---

## 5. DisplayModule — 2 нові сигнатури

LCD 20x4. Без змін користувачу — старий `show_main` має залишитись (overload).

`include/DisplayModule.h`:
```cpp
void show_main(Fault f, Mode m, uint16_t slot_idx, uint16_t slots_total,
               float x_mm, float y_mm, float z_mm, float rpm, bool sd_ok, const char *recipe_name);
void show_banner_sd_status(bool mounted, const char *recipe_name);
```

`src/DisplayModule.cpp` — `show_main` row3 rules:
- **IDLE / READY / FAULT / DONE**: row3 = `"SD:OK %-13.13s"` or `"SD:DEF %-12.12s"`
- **RUN_SLOT / RUN_HOME / RETRACT / PLUNGE etc.**: row3 = `"Slot %-3u/%-3u %-10.10s"` (slot_idx+1/total + recipe_name)

Banner splash (4 рядки 20x4):
```
Comb 80x1.0  F407ZET6
SD:OK   mounted
comb_latun_225
START -> HOME
```
(де 2-й рядок = SD:DEF + defaults якщо SD відсутня).

---

## 6. ProgramModule — замінити hardcodes AxisCfg → s_recipe.*

У робочому циклі (run_sequence state-machine) **ВСЬКИЙ** AxisCfg:: для динамічних параметрів замінюємо на `const RecipeData &recipe = s_recipe;`:
```cpp
RAPID_FEED_MM_MIN        → recipe.rapid_mm_min
Z_FEED_CUT_MM_MIN        → recipe.z_feed_cut_mm_min
Z_FEED_PLUNGE_MM_MIN     → recipe.z_feed_plunge_mm_min
MATERIAL_THICKNESS_MM    → recipe.material_thickness_mm
SLOT_OVERCUT_MM          → recipe.slot_overcut_mm
BACKLASH_X_MM            → recipe.backlash_x_mm
CORR_TOL_MM              → recipe.corr_tol_mm
FEED_CORR_MM_MIN         → recipe.feed_corr_mm_min
```
Цикл слотів: `for (uint16_t i = 0; i < recipe.slots_count; ++i)` (зверни увагу: `recipe.pass_count` = alias, якщо в коді вже використано `pass_count` — ОБОВ'ЯЗКОВО синхронізуй).

SSOT plunge depth: `z_plunge = recipe.material_thickness_mm + recipe.slot_overcut_mm` (не дублювати).

---

## 7. main.cpp — init order

Порядок ініціалізації **НЕ ЧІПАТИ** але вставити load_recipe + banner:
```cpp
// ... MX_GPIO_Init, MX_TIMx_Init, MX_ADC1_Init, HAL_Init / SystemClock ...
SafetyModule::init();       // 1-й: safe-state (ENA HIGH, Z BRAKE HIGH, SP EN LOW, FAULT ON)
SpindleModule::init();
MotorModule::init();
EncoderModule::init();
ScaleModule::init();
DisplayModule::init();
StorageModule::init();      // GPIO SDIO AF12: PC8..PC12, PD2 — незалежно від USE_FATFS

auto &s_recipe = StorageModule::recipe();
bool sd_ok = StorageModule::read_recipe(s_recipe);

DisplayModule::show_banner_sd_status(sd_ok, s_recipe.name);
HAL_Delay(1800);   // splash 1.8s

// ===== super-loop =====
while (true) {
  // ...
  DisplayModule::show_main(fault, mode, slot_idx, s_recipe.slots_count,
                           x,y,z,rpm, StorageModule::is_sd_ready(), s_recipe.name);
}
```

---

## 8. platformio.ini — USE_FATFS за замовчуванням ВИМК.

Закоментований define. Розкоментувати **лише** коли є BSP FatFS файли (`ff.c`, `ff.h`, `ff_gen_drv.c`, `diskio.c`, `stm32f4xx_hal_sd.c`, `sd_diskio.c`) **і** вставлена SD-картка:

```ini
build_flags =
    -D STM32F407xx
    -D USE_HAL_DRIVER
    -Os -ffunction-sections -fdata-sections -fno-rtti -fno-exceptions -std=gnu++17
    -Wall -Wextra
    ; ---- SD Card + FatFS (розкоментувати коли є карта + BSP FatFS) ----
    ;-D USE_FATFS
```

---

## 9. Формат RECIPE.TXT (FAT32, корінь)

Зразок для латунної гребінки 2.0 мм × 225 слотів:
```ini
; Comb latun recipe 2mm
RECIPE_NAME               = comb_latun_225
SLOTS_COUNT               = 225
PITCH_MM                  = 2.000
SLOT_WIDTH_MM             = 1.0
SLOT_DEPTH_MM             = 12.0
MATERIAL_THICKNESS_MM     = 3.0
SLOT_OVERCUT_MM           = 0.3
SPINDLE_RPM               = 250
FEED_MM_MIN               = 60
RAPID_MM_MIN              = 400
Z_APPROACH_MM             = 3.0
PLUNGE_FEED               = 15
Z_FEED                    = 30
BACKLASH_X_MM             = 0.020
CORR_TOL_MM               = 0.010
CORR_FEED                 = 100
```

Правила парсера:
- Коментарі: рядки що починаються з `#` або `;` → ігноруються
- Пробіли/таби навколо `KEY`, `VALUE`, навколо `=` → допустимі (trim)
- Невідомий ключ → ігнорується (не помилка)
- Пусті рядки → ігноруються
- Символи CR/LF → trim'ляться
- Рядки довші `LINE_MAX_LEN` (80) → обрізаються `f_gets` — не leading overflow

---

## 10. Build check — обов'язково SUCCESS

Після змін ЗАВЖДИ прогнати збірку:
```powershell
pio run -e genericSTM32F407ZET6 2>&1 | Select-Object -Last 30
```
**Очікуване**: `[SUCCESS]` 0 warnings, 0 errors. RAM ≈ 0.8%, Flash ≈ 3.7%.

Часті причини fail:
| Симптом | Причина | Фікс |
|---|---|---|
| 8× `-Wmisleading-indentation` L181..L209 apply_kv | `if(touched) X; return;` без дужок | Макрос `SET_CHANGED do {...} while(0)` + return окремо |
| `SLOT_STEP_MM is not a member of AxisCfg` | Відсутній default в Config.h | Додати §3 дефолти |
| `RPM_MIN is not a member of SpindleCfg` | Відсутній нижній ліміт RPM | Додати §3 SpindleCfg::RPM_MIN |
| Linker `undefined reference to f_mount` | USE_FATFS define без BSP файлів | Закоментувати `-D USE_FATFS` в platformio.ini |

---

## 11. Набір змін (diff size): ≈ 8 файлів

| Файл | Тип зміни |
|---|---|
| `include/Config.h` | +4 AxisCfg дефолта (SLOTS_COUNT, STEP/WIDTH/DEPTH) + SpindleCfg RPM_MIN |
| `include/StorageModule.h` | RecipeData (15 полів) + сигнатури API set_defaults/apply_kv/read_recipe/is_sd_ready |
| `src/StorageModule.cpp` | Реалізація: set_defaults, apply_kv з clamp 15 ключів, read_recipe f_mount/f_gets парсер, s_sd_ready, init GPIO SDIO |
| `include/DisplayModule.h` | Новий show_main(sd_ok, recipe_name) overload + show_banner_sd_status |
| `src/DisplayModule.cpp` | show_main row3 = SD:OK/DEF + recipe / Slot n/N + recipe; banner splash |
| `src/ProgramModule.cpp` | Усі AxisCfg:: динам. параметри → s_recipe.*; alias pass_count/slots_count |
| `src/main.cpp` | read_recipe() після StorageModule::init, banner 1.8s, show_main + 2 params |
| `platformio.ini` | `;-D USE_FATFS` (закоментовано) в build_flags |
