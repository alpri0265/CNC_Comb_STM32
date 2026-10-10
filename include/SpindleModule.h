#pragma once
#include "Config.h"

namespace SpindleModule {

void init();

void set_rpm(uint16_t rpm);
uint16_t get_rpm_setpoint();

void enable(bool on);
bool is_enabled();

void fault_stop(); // RPM=0 + EN=LOW, safe

} // namespace SpindleModule
