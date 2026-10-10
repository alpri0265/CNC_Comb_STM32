#pragma once
#include "Config.h"
#include <functional>

namespace SafetyModule {

enum class Fault : uint16_t {
    NONE     = 0x0000,
    ESTOP    = 0x0001,
    LIM_X_P  = 0x0002,
    LIM_X_M  = 0x0004,
    LIM_Y_P  = 0x0008,
    LIM_Y_M  = 0x0010,
    LIM_Z_P  = 0x0020,
    LIM_Z_M  = 0x0040,
    HOME_ERR = 0x0100,
};

void init();
void tick_1ms();

Fault active_faults();
bool  is_fault();
bool  estop_active();

bool home_switch_active(Axis a);
bool limit_plus_active(Axis a);
bool limit_minus_active(Axis a);

void  clear_faults(); // only safe to call when ESTOP released, motors disabled
void  trigger_fault(Fault f);

void  register_fault_callback(void (*cb)(Fault));

// Safe-state: drivers disabled, spindle 0 V, Z brake engaged, LEDs show FAULT
void  enter_safe_state();

} // namespace SafetyModule
