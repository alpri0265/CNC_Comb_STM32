#include "SafetyModule.h"
#include "MotorModule.h"
#include "SpindleModule.h"

namespace SafetyModule {

static volatile uint16_t s_fault_mask;
static void (*s_cb)(Fault) = nullptr;
static volatile bool s_estop_pending = false;
static bool s_homing_allow_neg[3] = { false, false, false };

struct Input {
    GPIO_TypeDef *port;
    uint16_t      pin;
    bool          active_level;
    uint16_t      debounce_ms;
    uint16_t      timer;
    bool          stable_val;
    bool          raw_last;
};

static Input s_estop;
static Input s_home[3];
static Input s_limp[3];
static Input s_limm[3];

static bool read_p(const Input &i) {
    return (HAL_GPIO_ReadPin(i.port, i.pin) == GPIO_PIN_SET) ? true : false;
}

static void debounce(Input &i)
{
    bool raw = read_p(i);
    if (raw != i.raw_last) { i.timer = 0; i.raw_last = raw; return; }
    if (i.timer < i.debounce_ms) { i.timer++; return; }
    i.stable_val = raw;
}

static void leds_init()
{
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {};
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin   = PinCfg::LED_STATUS_PIN | PinCfg::LED_FAULT_PIN;
    HAL_GPIO_Init(PinCfg::LED_GPIOx, &gpio);
    HAL_GPIO_WritePin(PinCfg::LED_GPIOx, PinCfg::LED_STATUS_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PinCfg::LED_GPIOx, PinCfg::LED_FAULT_PIN,  GPIO_PIN_SET); // FAULT LED on until cleared
}

static void inputs_init()
{
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = PinCfg::ESTOP_PIN;
    HAL_GPIO_Init(PinCfg::ESTOP_GPIOx, &gpio);

    gpio.Pin = PinCfg::X_HOME_PIN | PinCfg::X_LIMP_PIN | PinCfg::X_LIMM_PIN |
               PinCfg::Y_HOME_PIN | PinCfg::Y_LIMP_PIN | PinCfg::Y_LIMM_PIN |
               PinCfg::Z_HOME_PIN | PinCfg::Z_LIMP_PIN | PinCfg::Z_LIMM_PIN;
    HAL_GPIO_Init(GPIOF, &gpio);

    s_estop.port         = PinCfg::ESTOP_GPIOx;
    s_estop.pin          = PinCfg::ESTOP_PIN;
    s_estop.active_level = SafetyCfg::ESTOP_ACTIVE_LEVEL;
    s_estop.debounce_ms  = SafetyCfg::DEBOUNCE_MS;
    s_estop.timer        = 0;
    s_estop.raw_last     = read_p(s_estop);
    s_estop.stable_val   = s_estop.raw_last;

    GPIO_TypeDef *gx = PinCfg::X_LIM_GPIOx;
    GPIO_TypeDef *gy = PinCfg::Y_LIM_GPIOx;
    GPIO_TypeDef *gz = PinCfg::Z_LIM_GPIOx;
    // NOTE: stable_val і raw_last будуть перезаписані нижче в циклі for(i=0..2)
    // фактичним ReadPin з власного порту/піна. Тут placeholder = false/0.
    s_home[0] = { gx, PinCfg::X_HOME_PIN, SafetyCfg::HOME_ACTIVE_LEVEL,  SafetyCfg::DEBOUNCE_MS, 0, false, false };
    s_limp[0] = { gx, PinCfg::X_LIMP_PIN, SafetyCfg::LIMIT_ACTIVE_LEVEL, SafetyCfg::DEBOUNCE_MS, 0, false, false };
    s_limm[0] = { gx, PinCfg::X_LIMM_PIN, SafetyCfg::LIMIT_ACTIVE_LEVEL, SafetyCfg::DEBOUNCE_MS, 0, false, false };
    s_home[1] = { gy, PinCfg::Y_HOME_PIN, SafetyCfg::HOME_ACTIVE_LEVEL,  SafetyCfg::DEBOUNCE_MS, 0, false, false };
    s_limp[1] = { gy, PinCfg::Y_LIMP_PIN, SafetyCfg::LIMIT_ACTIVE_LEVEL, SafetyCfg::DEBOUNCE_MS, 0, false, false };
    s_limm[1] = { gy, PinCfg::Y_LIMM_PIN, SafetyCfg::LIMIT_ACTIVE_LEVEL, SafetyCfg::DEBOUNCE_MS, 0, false, false };
    s_home[2] = { gz, PinCfg::Z_HOME_PIN, SafetyCfg::HOME_ACTIVE_LEVEL,  SafetyCfg::DEBOUNCE_MS, 0, false, false };
    s_limp[2] = { gz, PinCfg::Z_LIMP_PIN, SafetyCfg::LIMIT_ACTIVE_LEVEL, SafetyCfg::DEBOUNCE_MS, 0, false, false };
    s_limm[2] = { gz, PinCfg::Z_LIMM_PIN, SafetyCfg::LIMIT_ACTIVE_LEVEL, SafetyCfg::DEBOUNCE_MS, 0, false, false };

    for (int i = 0; i < 3; i++) {
        s_home[i].raw_last = (HAL_GPIO_ReadPin(s_home[i].port, s_home[i].pin) == GPIO_PIN_SET);
        s_limp[i].raw_last = (HAL_GPIO_ReadPin(s_limp[i].port, s_limp[i].pin) == GPIO_PIN_SET);
        s_limm[i].raw_last = (HAL_GPIO_ReadPin(s_limm[i].port, s_limm[i].pin) == GPIO_PIN_SET);
        s_home[i].stable_val = s_home[i].raw_last;
        s_limp[i].stable_val = s_limp[i].raw_last;
        s_limm[i].stable_val = s_limm[i].raw_last;
    }
}

static void btns_init()
{
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {};
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = PinCfg::BTN_START_PIN | PinCfg::BTN_STOP_PIN;
    HAL_GPIO_Init(PinCfg::BTN_GPIOx, &gpio);
}

void init()
{
    leds_init();
    inputs_init();
    btns_init();
    s_fault_mask = (uint16_t)Fault::ESTOP; // пока ESTOP не отпущен — FAULT
    enter_safe_state();
}

void enter_safe_state()
{
    MotorModule::abort_all();
    MotorModule::enable_all(false);
    HAL_GPIO_WritePin(PinCfg::Z_GPIOx, PinCfg::Z_BRAKE_PIN, GPIO_PIN_SET);
    SpindleModule::fault_stop();
    HAL_GPIO_WritePin(PinCfg::LED_GPIOx, PinCfg::LED_STATUS_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PinCfg::LED_GPIOx, PinCfg::LED_FAULT_PIN,  GPIO_PIN_SET);
}

void tick_1ms()
{
    debounce(s_estop);
    for (int i = 0; i < 3; i++) {
        debounce(s_home[i]);
        debounce(s_limp[i]);
        debounce(s_limm[i]);
    }

    bool estop = (s_estop.stable_val == s_estop.active_level);
    if (estop) {
        if (!(s_fault_mask & (uint16_t)Fault::ESTOP)) {
            trigger_fault(Fault::ESTOP);
        }
    } else {
        // ESTOP released -> mask cleared by operator via clear_faults()
    }

    // Limits: Fault ТІЛЬКИ якщо вісь BUSY і напрямок руху У БІК активованого ліміту.
    // Якщо вісь стоїть (busy=false) або рух ВІД ліміту — дозволяємо escape з зони ліміту.
    // Для homing: s_homing_allow_neg[axis] == true → подавляємо MINUS Fault.
    const Fault lim_p[3] = { Fault::LIM_X_P, Fault::LIM_Y_P, Fault::LIM_Z_P };
    const Fault lim_m[3] = { Fault::LIM_X_M, Fault::LIM_Y_M, Fault::LIM_Z_M };
    for (int i = 0; i < 3; i++) {
        Axis ax = (Axis)i;
        bool busy         = MotorModule::is_busy(ax);
        bool dir_positive = MotorModule::get_direction(ax);
        bool p = (s_limp[i].stable_val == s_limp[i].active_level);
        bool m = (s_limm[i].stable_val == s_limm[i].active_level);
        (void)ax;

        if (p) {
            // Fault: рух В + сторону (dir_positive) + busy (загроза врізатися далі).
            const bool toward_plus = busy && dir_positive;
            if (toward_plus && !(s_fault_mask & (uint16_t)lim_p[i])) {
                trigger_fault(lim_p[i]);
            }
        }
        if (m) {
            if (s_homing_allow_neg[i]) continue; // homing режим: ігнор мінус-ліміт
            // Fault: рух В - сторону (dir_positive=false) + busy.
            const bool toward_minus = busy && !dir_positive;
            if (toward_minus && !(s_fault_mask & (uint16_t)lim_m[i])) {
                trigger_fault(lim_m[i]);
            }
        }
    }
}

Fault active_faults() { return (Fault)s_fault_mask; }
bool  is_fault()      { return s_fault_mask != 0; }
bool  estop_active()  { return (s_estop.stable_val == s_estop.active_level); }

bool home_switch_active(Axis a)
{
    int i = (int)a; if (i < 0 || i >= 3) return false;
    return (s_home[i].stable_val == s_home[i].active_level);
}
bool limit_plus_active(Axis a)
{
    int i = (int)a; if (i < 0 || i >= 3) return false;
    return (s_limp[i].stable_val == s_limp[i].active_level);
}
bool limit_minus_active(Axis a)
{
    int i = (int)a; if (i < 0 || i >= 3) return false;
    return (s_limm[i].stable_val == s_limm[i].active_level);
}

void trigger_fault(Fault f)
{
    s_fault_mask |= (uint16_t)f;
    enter_safe_state();
    if (s_cb) (*s_cb)(f);
}

void clear_faults()
{
    if (estop_active()) return; // must release ESTOP first
    s_fault_mask = 0;
    HAL_GPIO_WritePin(PinCfg::LED_GPIOx, PinCfg::LED_FAULT_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PinCfg::LED_GPIOx, PinCfg::LED_STATUS_PIN, GPIO_PIN_SET);
}

void register_fault_callback(void (*cb)(Fault)) { s_cb = cb; }

void set_homing_allow_neg(Axis a, bool allow)
{
    int i = (int)a; if (i < 0 || i >= 3) return;
    s_homing_allow_neg[i] = allow;
}

} // namespace SafetyModule
