#include "ProgramModule.h"
#include "MotorModule.h"
#include "ScaleModule.h"
#include "SpindleModule.h"
#include "SafetyModule.h"

namespace ProgramModule {

static StorageModule::RecipeData s_recipe;
static bool                      s_recipe_valid;
static volatile ProgramState     s_state;
static volatile ProgramState     s_resume_state;
static uint32_t                  s_slot;
static uint32_t                  s_substep_ms;
static float                     s_x_target;
static float                     s_y_target;
static float                     s_z_target;
static float                     s_y_start;
static float                     s_y_end;

static void enter(ProgramState s)
{
    s_state      = s;
    s_substep_ms = 0;
}

void init()
{
    StorageModule::set_defaults(s_recipe);
    s_recipe_valid = true;
    s_slot         = 0;
    s_x_target     = 0.0f;
    s_y_target     = 0.0f;
    s_z_target     = 0.0f;
    s_y_start      = 0.0f;
    s_y_end        = 0.0f;
    s_resume_state = ProgramState::IDLE;
    enter(ProgramState::IDLE);
}

void load_recipe()
{
    StorageModule::RecipeData r;
    bool ok = StorageModule::read_recipe(r);
    if (ok) {
        s_recipe = r;
        s_recipe_valid = true;
    } else {
        // defaults already in place; mark as valid anyway
        s_recipe_valid = true;
    }
}

bool has_recipe() { return s_recipe_valid; }
const StorageModule::RecipeData & recipe() { return s_recipe; }
ProgramState get_state() { return s_state; }
uint32_t     get_slot_index() { return s_slot; }

void start()
{
    if (SafetyModule::is_fault()) return;
    if (s_state == ProgramState::IDLE || s_state == ProgramState::DONE ||
        s_state == ProgramState::READY) {
        s_slot = 0;
        MotorModule::enable_all(true);
        ScaleModule::set_zero();
        MotorModule::reset_position(Axis::X, 0.0f);
        MotorModule::reset_position(Axis::Y, 0.0f);
        MotorModule::reset_position(Axis::Z, 0.0f);
        // 1) go Z to approach level first
        s_z_target = s_recipe.z_approach_mm;
        MotorModule::move_absolute_mm(Axis::Z, s_z_target, AxisCfg::RAPID_FEED_MM_MIN);
        s_state = ProgramState::APPROACH_Z;
    } else if (s_state == ProgramState::PAUSED) {
        resume();
    }
}

void pause()
{
    if (s_state == ProgramState::IDLE || s_state == ProgramState::DONE ||
        s_state == ProgramState::PAUSED || s_state == ProgramState::FAULT) return;
    MotorModule::abort_all();
    s_resume_state = s_state;
    enter(ProgramState::PAUSED);
}

void resume()
{
    if (s_state != ProgramState::PAUSED) return;
    MotorModule::enable_all(true);
    enter(s_resume_state);
}

void stop()
{
    MotorModule::abort_all();
    SpindleModule::set_rpm(0);
    SpindleModule::enable(false);
    // Retract Z to safe
    s_z_target = s_recipe.z_approach_mm;
    MotorModule::move_absolute_mm(Axis::Z, s_z_target, AxisCfg::Z_FEED_CUT_MM_MIN);
    while (MotorModule::is_busy(Axis::Z)) {}
    MotorModule::enable_all(false);
    enter(ProgramState::IDLE);
}

void fault_halt()
{
    SafetyModule::enter_safe_state();
    enter(ProgramState::FAULT);
}

static void state_idle()
{
    // operator must press START or long-press encoder
}

static void state_approach_z()
{
    if (!MotorModule::is_busy(Axis::Z)) {
        // Pre-position Y at start (first slot)
        s_y_start  = 0.0f;
        s_y_end    = s_recipe.slot_depth_mm; // for HSS blade slot 1x12mm: Y travels 12 mm
        s_x_target = 0.0f;
        MotorModule::move_absolute_mm(Axis::X, s_x_target, AxisCfg::RAPID_FEED_MM_MIN);
        MotorModule::move_absolute_mm(Axis::Y, s_y_start,  AxisCfg::RAPID_FEED_MM_MIN);
        enter(ProgramState::START_SPINDLE);
    }
}

static void state_start_spindle()
{
    if (!MotorModule::any_busy()) {
        if (s_substep_ms == 0) {
            SpindleModule::enable(true);
            SpindleModule::set_rpm(s_recipe.spindle_rpm);
        }
        if (s_substep_ms < 0xFFFFFFFFu) s_substep_ms++;
        if (s_substep_ms >= 1500u) {
            // Plunge Z first (brass 3 mm + 0.3 mm overcut)
            const float z_plunge = 3.3f;
            MotorModule::move_absolute_mm(Axis::Z,
                                          s_recipe.z_approach_mm - z_plunge,
                                          AxisCfg::Z_FEED_CUT_MM_MIN);
            // Y travel cuts the slot of slot_depth_mm length (12 mm by spec)
            MotorModule::move_absolute_mm(Axis::Y, s_y_end, s_recipe.feed_mm_min);
            enter(ProgramState::CUT_SLOT_Y);
        }
    }
}

static void state_cut_slot_y()
{
    if (!MotorModule::any_busy()) {
        // Retract Z above surface by z_approach_mm
        s_z_target = s_recipe.z_approach_mm;
        MotorModule::move_absolute_mm(Axis::Z, s_z_target, AxisCfg::Z_FEED_CUT_MM_MIN);
        enter(ProgramState::RETRACT_Z);
    }
}

static void state_retract_z()
{
    if (!MotorModule::is_busy(Axis::Z)) {
        ++s_slot;
        if (s_slot >= s_recipe.pass_count) {
            SpindleModule::set_rpm(0);
            SpindleModule::enable(false);
            enter(ProgramState::DONE);
            return;
        }
        // Index X: X += pitch_mm; approach from SAME side (always +X direction, one-side approach per spec
        // to remove backlash).
        float new_x = (float)s_slot * s_recipe.pitch_mm;
        s_x_target = new_x;
        // first move Y back to start while Z is safe
        MotorModule::move_absolute_mm(Axis::Y, s_y_start, AxisCfg::RAPID_FEED_MM_MIN);
        MotorModule::move_absolute_mm(Axis::X, s_x_target, AxisCfg::RAPID_FEED_MM_MIN);
        enter(ProgramState::INDEX_X);
    }
}

static void state_index_x()
{
    if (!MotorModule::any_busy()) {
        enter(ProgramState::START_SPINDLE);
        s_substep_ms = 0;
    }
}

void step()
{
    if (SafetyModule::is_fault() && s_state != ProgramState::FAULT) {
        fault_halt();
        return;
    }
    switch (s_state) {
        case ProgramState::IDLE:         state_idle();         break;
        case ProgramState::HOME_XYZ:                             break;
        case ProgramState::READY:                                break;
        case ProgramState::APPROACH_Z:   state_approach_z();   break;
        case ProgramState::START_SPINDLE:state_start_spindle(); break;
        case ProgramState::CUT_SLOT_Y:  state_cut_slot_y();    break;
        case ProgramState::RETRACT_Z:   state_retract_z();     break;
        case ProgramState::INDEX_X:     state_index_x();       break;
        case ProgramState::DONE:
        case ProgramState::PAUSED:
        case ProgramState::FAULT:       break;
    }
}

} // namespace ProgramModule
