#pragma once
#include "Config.h"

namespace MotorModule {

void init();

void enable_all(bool on);
void enable_axis(Axis a, bool on);

void set_direction(Axis a, bool positive);
bool get_direction(Axis a);

void move_pulses(Axis a, int32_t pulses, uint32_t step_freq_hz);
void move_mm(Axis a, float delta_mm, float feed_mm_min);
void move_absolute_mm(Axis a, float abs_mm, float feed_mm_min);

void abort(Axis a);
void abort_all();

bool is_busy(Axis a);
bool any_busy();

int32_t get_position_pulses(Axis a);
float   get_position_mm(Axis a);
void    reset_position(Axis a, float mm = 0.0f);

void on_timer_update(Axis a);

} // namespace MotorModule
