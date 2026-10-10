#pragma once
#include "Config.h"

namespace StorageModule {

struct RecipeData {
    char     name[Recipe::VALUE_MAX_LEN];
    uint32_t pass_count;
    float    pitch_mm;
    float    slot_width_mm;
    float    slot_depth_mm;
    uint16_t spindle_rpm;
    float    feed_mm_min;
    float    z_approach_mm;
};

void init();
bool is_sd_ready();

void set_defaults(RecipeData &r);
bool parse_line(const char *line, char *key, char *value); // returns false for comment/blank
void apply_kv(RecipeData &r, const char *key, const char *value);

bool read_recipe(RecipeData &out); // true if SD read successful; otherwise fills defaults

} // namespace StorageModule
