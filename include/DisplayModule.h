#pragma once
#include "Config.h"

namespace DisplayModule {

void init();

void clear();
void home();
void set_cursor(uint8_t col, uint8_t row);
void print(const char *s);
void printf(const char *fmt, ...);

void backlight(bool on);

void show_main(float x_mm_cmd, float x_mm_scale, float y_mm, float z_mm,
               uint16_t spindle_rpm, ProgramState state,
               uint32_t slot_idx, uint32_t slot_total,
               bool sd_ok, const char *recipe_name);

void show_banner_sd_status(bool mounted, const char *recipe_name);

} // namespace DisplayModule
