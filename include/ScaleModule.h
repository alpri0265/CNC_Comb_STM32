#pragma once
#include "Config.h"

namespace ScaleModule {

void init();

float   get_position_mm();
int32_t get_raw_count();

void set_zero();
void set_position_mm(float mm);

} // namespace ScaleModule
