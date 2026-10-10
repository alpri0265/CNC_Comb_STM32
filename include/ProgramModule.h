#pragma once
#include "Config.h"
#include "StorageModule.h"

namespace ProgramModule {

void init();

void load_recipe();
bool has_recipe();
const StorageModule::RecipeData & recipe();

ProgramState get_state();
uint32_t     get_slot_index();

void start();   // IDLE -> READY or HOME (if needed)
void pause();
void resume();
void stop();    // motors abort + spindle 0 + retract Z

// Main state-machine step — call from super-loop, non-blocking
void step();

void fault_halt();

} // namespace ProgramModule
