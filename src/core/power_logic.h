#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BC250_POWER_UNKNOWN = 0,
    BC250_POWER_OFF,
    BC250_POWER_STARTING,
    BC250_POWER_ON,
    BC250_POWER_STOPPING,
    BC250_POWER_FAULT,
} bc250_power_state_t;

typedef enum {
    BC250_POWER_ACTION_NONE = 0,
    BC250_POWER_ACTION_ON,
    BC250_POWER_ACTION_OFF,
    BC250_POWER_ACTION_TOGGLE,
    BC250_POWER_ACTION_FORCE_OFF,
} bc250_power_action_t;

typedef enum {
    BC250_START_PS_ON_ONLY = 0,
    BC250_START_BUTTON_ONLY,
    BC250_START_PS_ON_THEN_BUTTON,
    BC250_START_SIMULTANEOUS,
} bc250_start_strategy_t;

typedef struct {
    bc250_start_strategy_t strategy;
    uint32_t inter_output_delay_ms;
    uint32_t button_pulse_ms;
    uint32_t handoff_delay_ms;
    uint32_t start_timeout_ms;
    uint32_t shutdown_timeout_ms;
    uint32_t force_off_ms;
    uint32_t retry_cooldown_ms;
} bc250_power_timing_t;

typedef struct {
    bool ps_on;
    bool power_button;
} bc250_power_outputs_t;

typedef struct {
    bc250_power_state_t state;
    bc250_power_timing_t timing;
    bc250_power_outputs_t outputs;
    uint64_t phase_started_ms;
    uint64_t state_started_ms;
    uint64_t last_failure_ms;
    bool button_pulse_completed;
    bool handoff_started;
    bool initialized;
} bc250_power_logic_t;

bc250_power_timing_t bc250_power_default_timing(void);
void bc250_power_logic_init(bc250_power_logic_t *logic, const bc250_power_timing_t *timing,
                            bool sensed_on, uint64_t now_ms);
bool bc250_power_request(bc250_power_logic_t *logic, bc250_power_action_t action,
                         bool sensed_on, uint64_t now_ms);
void bc250_power_tick(bc250_power_logic_t *logic, bool sensed_on, uint64_t now_ms);
const char *bc250_power_state_name(bc250_power_state_t state);

#ifdef __cplusplus
}
#endif

