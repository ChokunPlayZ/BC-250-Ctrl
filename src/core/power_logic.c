#include "power_logic.h"

#include <stddef.h>
#include <string.h>

static uint64_t elapsed(uint64_t now, uint64_t then)
{
    return now >= then ? now - then : 0;
}

static void outputs_off(bc250_power_logic_t *logic)
{
    logic->outputs.ps_on = false;
    logic->outputs.power_button = false;
}

static void enter_state(bc250_power_logic_t *logic, bc250_power_state_t state, uint64_t now_ms)
{
    logic->state = state;
    logic->state_started_ms = now_ms;
    logic->phase_started_ms = now_ms;
}

bc250_power_timing_t bc250_power_default_timing(void)
{
    return (bc250_power_timing_t) {
        .strategy = BC250_START_PS_ON_THEN_BUTTON,
        .inter_output_delay_ms = 500,
        .button_pulse_ms = 250,
        .handoff_delay_ms = 1000,
        .start_timeout_ms = 15000,
        .shutdown_timeout_ms = 120000,
        .force_off_ms = 5000,
        .retry_cooldown_ms = 60000,
    };
}

void bc250_power_logic_init(bc250_power_logic_t *logic, const bc250_power_timing_t *timing,
                            bool sensed_on, uint64_t now_ms)
{
    if (logic == NULL) {
        return;
    }
    memset(logic, 0, sizeof(*logic));
    logic->timing = timing != NULL ? *timing : bc250_power_default_timing();
    logic->state = sensed_on ? BC250_POWER_ON : BC250_POWER_OFF;
    logic->state_started_ms = now_ms;
    logic->phase_started_ms = now_ms;
    logic->initialized = true;
    outputs_off(logic);
}

static bool begin_start(bc250_power_logic_t *logic, uint64_t now_ms)
{
    if (logic->last_failure_ms != 0 &&
        elapsed(now_ms, logic->last_failure_ms) < logic->timing.retry_cooldown_ms) {
        return false;
    }
    outputs_off(logic);
    logic->button_pulse_completed = false;
    logic->handoff_started = false;
    enter_state(logic, BC250_POWER_STARTING, now_ms);

    switch (logic->timing.strategy) {
    case BC250_START_PS_ON_ONLY:
        logic->outputs.ps_on = true;
        logic->button_pulse_completed = true;
        break;
    case BC250_START_BUTTON_ONLY:
        logic->outputs.power_button = true;
        break;
    case BC250_START_SIMULTANEOUS:
        logic->outputs.ps_on = true;
        logic->outputs.power_button = true;
        break;
    case BC250_START_PS_ON_THEN_BUTTON:
    default:
        logic->outputs.ps_on = true;
        if (logic->timing.inter_output_delay_ms == 0) {
            logic->outputs.power_button = true;
        }
        break;
    }
    return true;
}

static bool begin_stop(bc250_power_logic_t *logic, bool force, uint64_t now_ms)
{
    outputs_off(logic);
    logic->outputs.power_button = true;
    logic->button_pulse_completed = false;
    enter_state(logic, BC250_POWER_STOPPING, now_ms);
    if (force) {
        logic->phase_started_ms = now_ms - logic->timing.button_pulse_ms;
    }
    return true;
}

bool bc250_power_request(bc250_power_logic_t *logic, bc250_power_action_t action,
                         bool sensed_on, uint64_t now_ms)
{
    if (logic == NULL || !logic->initialized) {
        return false;
    }
    if (action == BC250_POWER_ACTION_TOGGLE) {
        action = sensed_on ? BC250_POWER_ACTION_OFF : BC250_POWER_ACTION_ON;
    }
    switch (action) {
    case BC250_POWER_ACTION_ON:
        if (logic->state == BC250_POWER_STOPPING) {
            return false;
        }
        if (sensed_on || logic->state == BC250_POWER_STARTING) {
            return true;
        }
        return begin_start(logic, now_ms);
    case BC250_POWER_ACTION_OFF:
        if (logic->state == BC250_POWER_STARTING) {
            outputs_off(logic);
            enter_state(logic, BC250_POWER_OFF, now_ms);
            return true;
        }
        if (!sensed_on || logic->state == BC250_POWER_STOPPING) {
            return true;
        }
        return begin_stop(logic, false, now_ms);
    case BC250_POWER_ACTION_FORCE_OFF:
        if (logic->state == BC250_POWER_STARTING) {
            outputs_off(logic);
            enter_state(logic, BC250_POWER_OFF, now_ms);
            return true;
        }
        if (!sensed_on) {
            return true;
        }
        outputs_off(logic);
        enter_state(logic, BC250_POWER_STOPPING, now_ms);
        logic->outputs.power_button = true;
        logic->button_pulse_completed = true;
        return true;
    case BC250_POWER_ACTION_NONE:
    default:
        return false;
    }
}

static void tick_starting(bc250_power_logic_t *logic, bool sensed_on, uint64_t now_ms)
{
    uint64_t state_age = elapsed(now_ms, logic->state_started_ms);

    if (sensed_on) {
        logic->outputs.power_button = false;
        logic->button_pulse_completed = true;
    } else if (!logic->button_pulse_completed) {
        bool begin_button = logic->timing.strategy == BC250_START_BUTTON_ONLY ||
                            logic->timing.strategy == BC250_START_SIMULTANEOUS ||
                            state_age >= logic->timing.inter_output_delay_ms;
        if (begin_button && !logic->outputs.power_button) {
            logic->outputs.power_button = true;
            logic->phase_started_ms = now_ms;
        }
        if (logic->outputs.power_button &&
            elapsed(now_ms, logic->phase_started_ms) >= logic->timing.button_pulse_ms) {
            logic->outputs.power_button = false;
            logic->button_pulse_completed = true;
        }
    }

    if (sensed_on) {
        if (!logic->handoff_started) {
            logic->handoff_started = true;
            logic->phase_started_ms = now_ms;
        }
        if (elapsed(now_ms, logic->phase_started_ms) >= logic->timing.handoff_delay_ms) {
            outputs_off(logic);
            enter_state(logic, BC250_POWER_ON, now_ms);
        }
        return;
    }

    if (state_age >= logic->timing.start_timeout_ms) {
        outputs_off(logic);
        logic->last_failure_ms = now_ms;
        enter_state(logic, BC250_POWER_FAULT, now_ms);
    }
}

static void tick_stopping(bc250_power_logic_t *logic, bool sensed_on, uint64_t now_ms)
{
    uint64_t state_age = elapsed(now_ms, logic->state_started_ms);

    if (logic->outputs.power_button) {
        uint32_t hold_ms = logic->button_pulse_completed ? logic->timing.force_off_ms
                                                         : logic->timing.button_pulse_ms;
        if (elapsed(now_ms, logic->phase_started_ms) >= hold_ms) {
            logic->outputs.power_button = false;
            logic->button_pulse_completed = true;
        }
    }
    if (!sensed_on) {
        outputs_off(logic);
        enter_state(logic, BC250_POWER_OFF, now_ms);
    } else if (state_age >= logic->timing.shutdown_timeout_ms) {
        outputs_off(logic);
        logic->last_failure_ms = now_ms;
        enter_state(logic, BC250_POWER_FAULT, now_ms);
    }
}

void bc250_power_tick(bc250_power_logic_t *logic, bool sensed_on, uint64_t now_ms)
{
    if (logic == NULL || !logic->initialized) {
        return;
    }
    switch (logic->state) {
    case BC250_POWER_STARTING:
        tick_starting(logic, sensed_on, now_ms);
        break;
    case BC250_POWER_STOPPING:
        tick_stopping(logic, sensed_on, now_ms);
        break;
    case BC250_POWER_ON:
        if (!sensed_on) {
            outputs_off(logic);
            enter_state(logic, BC250_POWER_OFF, now_ms);
        }
        break;
    case BC250_POWER_OFF:
        if (sensed_on) {
            outputs_off(logic);
            enter_state(logic, BC250_POWER_ON, now_ms);
        }
        break;
    case BC250_POWER_FAULT:
        if (sensed_on) {
            enter_state(logic, BC250_POWER_ON, now_ms);
        } else if (elapsed(now_ms, logic->last_failure_ms) >= logic->timing.retry_cooldown_ms) {
            enter_state(logic, BC250_POWER_OFF, now_ms);
        }
        break;
    case BC250_POWER_UNKNOWN:
    default:
        outputs_off(logic);
        enter_state(logic, sensed_on ? BC250_POWER_ON : BC250_POWER_OFF, now_ms);
        break;
    }
}

const char *bc250_power_state_name(bc250_power_state_t state)
{
    switch (state) {
    case BC250_POWER_OFF: return "off";
    case BC250_POWER_STARTING: return "starting";
    case BC250_POWER_ON: return "on";
    case BC250_POWER_STOPPING: return "stopping";
    case BC250_POWER_FAULT: return "fault";
    case BC250_POWER_UNKNOWN:
    default: return "unknown";
    }
}
