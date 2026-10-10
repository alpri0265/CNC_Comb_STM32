#pragma once
#include "Config.h"

namespace EncoderModule {

void init();

int32_t get_diff_and_reset();
bool    short_press();
bool    long_press();

void on_exti_a();
void on_exti_b();
void tick(); // 1 ms tick for key debounce / long press

} // namespace EncoderModule
