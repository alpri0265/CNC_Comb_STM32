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
static uint8_t                   s_homing_phase;  // 0..8 = Z(0-2) X(3-5) Y(6-8)
static uint32_t                  s_homing_axis_t0_ms; // HAL_GetTick() при початку кожної осі
static uint8_t                   s_homing_backoff_tries;
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
    s_homing_phase = 0;
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
    if (s_state == ProgramState::IDLE) {
        // IDLE → спочатку Homing XYZ
        s_slot = 0;
        s_homing_phase = 0;
        MotorModule::enable_all(true); // також звільняє Z-гальмо
        enter(ProgramState::HOME_XYZ);
    } else if (s_state == ProgramState::READY ||
               s_state == ProgramState::DONE) {
        // READY (після homing) або DONE (повторити цикл) → APPROACH_Z = початок циклу
        s_slot = 0;
        ScaleModule::set_zero();
        MotorModule::reset_position(Axis::X, 0.0f);
        MotorModule::reset_position(Axis::Y, 0.0f);
        MotorModule::reset_position(Axis::Z, 0.0f);
        MotorModule::enable_all(true);
        s_z_target = s_recipe.z_approach_mm;
        MotorModule::move_absolute_mm(Axis::Z, s_z_target, s_recipe.rapid_mm_min);
        enter(ProgramState::APPROACH_Z);
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
    // ❗ Неблокуючий: всі while() замінено на state STOPPING.
    MotorModule::abort_all();                 // Non-blocking
    SpindleModule::set_rpm(0);                // Non-blocking (CCR)
    SpindleModule::enable(false);             // Non-blocking (GPIO)
    // Retract Z до безпечного рівня (виконується наступним step() у стані STOPPING)
    s_z_target = s_recipe.z_approach_mm;
    MotorModule::move_absolute_mm(Axis::Z, s_z_target, s_recipe.z_feed_cut_mm_min);
    enter(ProgramState::STOPPING);
}

static void state_stopping()
{
    // Чекаємо доки Z не зупиниться; потім вимикаємо драйвери + IDLE.
    if (!MotorModule::is_busy(Axis::Z)) {
        MotorModule::enable_all(false);
        enter(ProgramState::IDLE);
    }
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
        MotorModule::move_absolute_mm(Axis::X, s_x_target, s_recipe.rapid_mm_min);
        MotorModule::move_absolute_mm(Axis::Y, s_y_start,  s_recipe.rapid_mm_min);
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
            // Plunge Z: товщина заготовки + переріз (гарантований проріз)
            const float z_plunge = s_recipe.material_thickness_mm + s_recipe.slot_overcut_mm;
            MotorModule::move_absolute_mm(Axis::Z,
                                          s_recipe.z_approach_mm - z_plunge,
                                          s_recipe.z_feed_plunge_mm_min);
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
        MotorModule::move_absolute_mm(Axis::Z, s_z_target, s_recipe.z_feed_cut_mm_min);
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
        // Index X: pitch_mm кожен прохід; ОДНОСТОРІННІЙ підхід з +X:
        //   target_x = slot * pitch
        //   overshoot_x = target_x + BACKLASH_X_MM + 0.02 мм
        //   рух Y→start та X→overshoot (паралельно)
        // Потім в state_index_x: X рух назад до target (retract from overshoot)
        //    → CORRECT_X
        float new_x       = (float)s_slot * s_recipe.pitch_mm;
        s_x_target        = new_x;
        float overshoot   = new_x + s_recipe.backlash_x_mm + 0.020f;
        if (overshoot > AxisCfg::X_MAX_MM) overshoot = AxisCfg::X_MAX_MM; // X_MAX — обмеження машини
        MotorModule::move_absolute_mm(Axis::Y, s_y_start, s_recipe.rapid_mm_min);
        MotorModule::move_absolute_mm(Axis::X, overshoot, s_recipe.rapid_mm_min);
        enter(ProgramState::INDEX_X);
    }
}

static void state_index_x()
{
    if (!MotorModule::any_busy()) {
        // Retract X з overshoot до цілі s_x_target (односторонній підхід: рух –X)
        MotorModule::move_absolute_mm(Axis::X, s_x_target, s_recipe.rapid_mm_min);
        s_substep_ms = 1; // ознака "retract запущено"
        return;
    }
    // Чекаємо поки retract X до цілі завершиться → перехід до корекції за шкалою
    if (s_substep_ms == 1 && !MotorModule::is_busy(Axis::X)) {
        s_substep_ms = 0;
        enter(ProgramState::CORRECT_X);
    }
}

static void state_correct_x()
{
    // Корекція «виміряв — доправив» по лінійній шкалі X.
    // s_substep_ms=0: вимір scale, обчислення delta,
    //   якщо |delta| <= CORR_TOL_MM → START_SPINDLE,
    //   інакше рух X на delta, s_substep_ms=1.
    // s_substep_ms=1: дочекатись is_busy(X)=false → START_SPINDLE.
    if (s_substep_ms == 0) {
        float x_scale = ScaleModule::get_position_mm();
        float delta   = s_x_target - x_scale;
        const float tol = s_recipe.corr_tol_mm;
        if (delta >  tol || delta < -tol) {
            MotorModule::move_mm(Axis::X, delta, s_recipe.feed_corr_mm_min);
            s_substep_ms = 1;
            return;
        }
        enter(ProgramState::START_SPINDLE);
        s_substep_ms = 0;
        return;
    }
    if (s_substep_ms == 1 && !MotorModule::is_busy(Axis::X)) {
        enter(ProgramState::START_SPINDLE);
        s_substep_ms = 0;
    }
}

static void state_home_xyz()
{
    // Послідовність: Z(0-2) → X(3-5) → Y(6-8).
    // Кожна вісь: substep 0 = fast -MAX_MM → home_switch → abort
    //             substep 1 = +BACKOFF_MM, дочекатись !home_switch
    //             substep 2 = slow -(BACKOFF+1) → home → abort → reset_position(0)
    // Після X-done: ScaleModule::set_zero(). Після Y-done: enter READY + вимкнути homing_allow.
    static const Axis   ax_seq[3]  = { Axis::Z, Axis::X, Axis::Y };
    static const float  max_seq[3] = { AxisCfg::Z_MAX_MM, AxisCfg::X_MAX_MM, AxisCfg::Y_MAX_MM };

    uint8_t grp = s_homing_phase / 3;   // 0=Z, 1=X, 2=Y
    uint8_t sub = s_homing_phase % 3;   // 0,1,2
    if (grp >= 3) {
        // Homing done.
        SafetyModule::set_homing_allow_neg(Axis::X, false);
        SafetyModule::set_homing_allow_neg(Axis::Y, false);
        SafetyModule::set_homing_allow_neg(Axis::Z, false);
        enter(ProgramState::READY);
        return;
    }
    Axis  a   = ax_seq[grp];
    float max = max_seq[grp];

    // Watchdog: watchdog на вісь (захист від застряглих рухів або сенсорів).
    // Перевіряємо ТІЛЬКИ коли рух вже запущено (s_substep_ms != 0), бо інакше
    // при up_time > 60с з моменту bootup + t0_ms=0 (неініціалізований) = false HOME_ERR.
    const uint32_t now_ms = HAL_GetTick();
    if (s_substep_ms != 0 && (now_ms - s_homing_axis_t0_ms) > AxisCfg::HOME_AXIS_TIMEOUT_MS) {
        SafetyModule::trigger_fault(SafetyModule::Fault::HOME_ERR);
        fault_halt();
        return;
    }

    switch (sub) {
    case 0: { // FAST search: рух –max до home_switch
        if (s_substep_ms == 0) {
            // Початок нової осі: ініціалізувати watchdog та лічильник backoff
            s_homing_axis_t0_ms    = now_ms;
            s_homing_backoff_tries = 0;
            SafetyModule::set_homing_allow_neg(a, true);
            MotorModule::enable_axis(a, true);
            if (a == Axis::Z) {
                // Z гальмо звільнити: enable_all(false) у safe-state, треба окремо.
                HAL_GPIO_WritePin(PinCfg::Z_GPIOx, PinCfg::Z_BRAKE_PIN, GPIO_PIN_RESET);
            }
            MotorModule::move_mm(a, -max, AxisCfg::HOME_FAST_MM_MIN);
            s_substep_ms = 1; // маркер "рух запущено"
            return;
        }
        // Чекаємо home_switch
        if (SafetyModule::home_switch_active(a)) {
            MotorModule::abort(a);
            ++s_homing_phase; // перейти в substep 1 (backoff)
            s_substep_ms = 0;
            return;
        }
        // Якщо добралися до ліміту мінус (home не знайшли): Fault HOME_ERR
        if (!MotorModule::is_busy(a) && !SafetyModule::home_switch_active(a)) {
            SafetyModule::trigger_fault(SafetyModule::Fault::HOME_ERR);
            fault_halt();
            return;
        }
    } break;
    case 1: { // BACKOFF: +HOME_BACKOFF_MM, дочекатись !home_switch
        if (s_substep_ms == 0) {
            MotorModule::move_mm(a, +AxisCfg::HOME_BACKOFF_MM, AxisCfg::HOME_FAST_MM_MIN);
            s_substep_ms = 1;
            return;
        }
        if (!MotorModule::is_busy(a)) {
            if (SafetyModule::home_switch_active(a)) {
                // backoff був замалий; ще один backoff але не більше HOME_BACKOFF_RETRIES_MAX
                if (s_homing_backoff_tries < AxisCfg::HOME_BACKOFF_RETRIES_MAX) {
                    ++s_homing_backoff_tries;
                    s_substep_ms = 0; // знову запустити move +BACKOFF
                    return;
                }
                // Перевищено ліміт повторів: home сенсор залипає/несправний
                SafetyModule::trigger_fault(SafetyModule::Fault::HOME_ERR);
                fault_halt();
                return;
            }
            ++s_homing_phase;
            s_substep_ms = 0;
            return;
        }
    } break;
    case 2: { // SLOW approach –(BACKOFF_MM + 1mm) доки home → abort → reset 0
        if (s_substep_ms == 0) {
            MotorModule::move_mm(a, -(AxisCfg::HOME_BACKOFF_MM + 1.0f), AxisCfg::HOME_SLOW_MM_MIN);
            s_substep_ms = 1;
            return;
        }
        if (SafetyModule::home_switch_active(a)) {
            MotorModule::abort(a);
            MotorModule::reset_position(a, 0.0f);
            if (a == Axis::X) {
                ScaleModule::set_zero(); // нуль шкали = X-home
            }
            SafetyModule::set_homing_allow_neg(a, false);
            ++s_homing_phase; // наступна вісь
            s_substep_ms = 0;
            return;
        }
        if (!MotorModule::is_busy(a) && !SafetyModule::home_switch_active(a)) {
            // не знайшли home після backoff
            SafetyModule::trigger_fault(SafetyModule::Fault::HOME_ERR);
            fault_halt();
            return;
        }
    } break;
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
        case ProgramState::HOME_XYZ:     state_home_xyz();     break;
        case ProgramState::READY:        /* чекаєм Start */    break;
        case ProgramState::APPROACH_Z:   state_approach_z();   break;
        case ProgramState::START_SPINDLE:state_start_spindle(); break;
        case ProgramState::CUT_SLOT_Y:  state_cut_slot_y();    break;
        case ProgramState::RETRACT_Z:   state_retract_z();     break;
        case ProgramState::INDEX_X:     state_index_x();       break;
        case ProgramState::CORRECT_X:   state_correct_x();     break;
        case ProgramState::STOPPING:    state_stopping();      break;
        case ProgramState::DONE:
        case ProgramState::PAUSED:
        case ProgramState::FAULT:       break;
    }
}

} // namespace ProgramModule
