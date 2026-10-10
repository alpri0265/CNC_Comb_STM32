#pragma once
#include "Config.h"

namespace StorageModule {

// ============================================================
// Рецепт робочого циклу (файл RECIPE.TXT на SD-карті, формат KEY=VALUE).
// Значення за замовчанням — з Config.h (AxisCfg/SpindleCfg).
// ============================================================
struct RecipeData {
    char     name[Recipe::VALUE_MAX_LEN];
    uint16_t slots_count;      // synonym PASS_COUNT / SLOTS_COUNT (кількість прорізів)
    uint16_t pass_count;       // [deprecated alias] дорівнює slots_count (для зворотної сумісності)
    float    pitch_mm;         // крок між пазами, 2.000 мм типово
    float    slot_width_mm;    // ширина паза = товщина пилки
    float    slot_depth_mm;    // довжина паза по Y, 12 мм типово
    uint16_t spindle_rpm;      // оберти шпинделя (SD300: 0..3000 = 0..10 В)
    float    feed_mm_min;      // подача Y при різанні
    float    z_approach_mm;    // Z-позиція підходу (над деталлю)

    // Додаткові параметри (можна змінювати без перепрошивки):
    float    rapid_mm_min;     // подача X/Y traverse
    float    material_thickness_mm; // товщина латунної заготовки (3 мм типово)
    float    slot_overcut_mm;  // переріз, гарантований проріз (0.3 мм)
    float    z_feed_plunge_mm_min;  // подача Z при зануренні в латунь
    float    z_feed_cut_mm_min;      // подача Z при різанні/підході
    float    backlash_x_mm;    // люфт X (SFU1605 типово 0.020)
    float    corr_tol_mm;      // допуск корекції X за шкалою (10 мкм)
    float    feed_corr_mm_min; // подача під час корекції X
};

void init();
bool is_sd_ready();

void set_defaults(RecipeData &r);
bool parse_line(const char *line, char *key, char *value);            // false = comment/blank
void apply_kv(RecipeData &r, const char *key, const char *value,
              bool *has_changes = nullptr);                          // з range validation

// Якщо SD не підключена, або файл не існує/непарситься →
//     заповнює defaults і повертає false (fallback).
// Успішне читання з SD → true, out має валідні дані.
bool read_recipe(RecipeData &out);

} // namespace StorageModule
