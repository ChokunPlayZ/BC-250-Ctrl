#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../../../src/core/power_logic.h"
#include "../../../src/core/ble_match.h"
#include "../../../src/core/button_logic.h"
#include "../../../src/core/presence_logic.h"
#include "../../../src/core/hp_commonslot_protocol.h"

static void test_hp_commonslot_protocol(void)
{
    uint8_t command[2];
    bc250_hp_commonslot_read_command(0x5f, 0x08, command);
    assert(command[0] == 0x08 && command[1] == 0x3a);
    bc250_hp_commonslot_read_command(0x58, 0x40, command);
    assert(command[0] == 0x40 && command[1] == 0x10);

    uint16_t raw = 0;
    const uint8_t good[] = {0x00, 0x0f, 0xf1};
    const uint8_t bad[] = {0x00, 0x0f, 0xf0};
    assert(bc250_hp_commonslot_decode_reply(good, &raw) && raw == 0x0f00);
    assert(!bc250_hp_commonslot_decode_reply(bad, &raw));
    assert(bc250_hp_commonslot_scale(0, raw) == 120.0f);
    assert(bc250_hp_commonslot_scale(2, 0x0c00) == 12.0f);
    assert(bc250_hp_commonslot_temperature_c(32 * 32) == 0.0f);
    float temperature_c = bc250_hp_commonslot_temperature_c(122 * 32);
    assert(temperature_c > 49.99f && temperature_c < 50.01f);
}

static void test_start_sequence(void)
{
    bc250_power_logic_t p;
    bc250_power_timing_t timing = bc250_power_default_timing();
    bc250_power_logic_init(&p, &timing, false, 0, false, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 1));
    assert(p.outputs.ps_on);
    assert(!p.outputs.power_button);

    bc250_power_tick(&p, false, 501);
    assert(p.outputs.power_button);
    bc250_power_tick(&p, false, 751);
    assert(!p.outputs.power_button);
    bc250_power_tick(&p, true, 1000);
    assert(p.outputs.ps_on);
    bc250_power_tick(&p, true, 2000);
    assert(p.state == BC250_POWER_ON);
    assert(!p.outputs.ps_on && !p.outputs.power_button);
}

static void test_start_timeout(void)
{
    bc250_power_logic_t p;
    bc250_power_timing_t timing = bc250_power_default_timing();
    bc250_power_logic_init(&p, &timing, false, 0, false, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 1));
    bc250_power_tick(&p, false, 15001);
    assert(p.state == BC250_POWER_FAULT);
    assert(!p.outputs.ps_on && !p.outputs.power_button);
    assert(!bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 15002));
}

static void test_start_strategies_and_idempotence(void)
{
    bc250_power_logic_t p;
    bc250_power_timing_t timing = bc250_power_default_timing();

    timing.strategy = BC250_START_PS_ON_ONLY;
    bc250_power_logic_init(&p, &timing, false, 0, false, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 1));
    assert(p.outputs.ps_on && !p.outputs.power_button);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 2));

    timing.strategy = BC250_START_BUTTON_ONLY;
    bc250_power_logic_init(&p, &timing, false, 0, false, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 1));
    assert(!p.outputs.ps_on && p.outputs.power_button);

    timing.strategy = BC250_START_SIMULTANEOUS;
    bc250_power_logic_init(&p, &timing, false, 0, false, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 1));
    assert(p.outputs.ps_on && p.outputs.power_button);
}

static void test_zero_delay_and_fast_sense(void)
{
    bc250_power_logic_t p;
    bc250_power_timing_t timing = bc250_power_default_timing();
    timing.inter_output_delay_ms = 0;
    bc250_power_logic_init(&p, &timing, false, 0, false, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 1));
    assert(p.outputs.ps_on && p.outputs.power_button);

    timing.inter_output_delay_ms = 500;
    bc250_power_logic_init(&p, &timing, false, 0, false, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 1));
    bc250_power_tick(&p, true, 200);
    assert(p.outputs.ps_on && !p.outputs.power_button);
    bc250_power_tick(&p, true, 600);
    assert(!p.outputs.power_button);
}

static void test_conflicting_commands_deassert_outputs(void)
{
    bc250_power_logic_t p;
    bc250_power_timing_t timing = bc250_power_default_timing();
    bc250_power_logic_init(&p, &timing, false, 0, false, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 1));
    assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, false, 2));
    assert(p.state == BC250_POWER_OFF);
    assert(!p.outputs.ps_on && !p.outputs.power_button);

    bc250_power_logic_init(&p, &timing, false, 0, true, 100);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, true, 101));
    assert(!bc250_power_request(&p, BC250_POWER_ACTION_ON, true, 102));
    assert(p.state == BC250_POWER_STOPPING);
}

static void test_graceful_and_force_off(void)
{
    bc250_power_logic_t p;
    bc250_power_timing_t timing = bc250_power_default_timing();
    bc250_power_logic_init(&p, &timing, false, 0, true, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, true, 1));
    assert(p.outputs.power_button);
    bc250_power_tick(&p, true, 251);
    assert(!p.outputs.power_button);
    bc250_power_tick(&p, false, 1000);
    assert(p.state == BC250_POWER_OFF);

    bc250_power_logic_init(&p, &timing, false, 0, true, 2000);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_FORCE_OFF, true, 2001));
    bc250_power_tick(&p, true, 6999);
    assert(p.outputs.power_button);
    bc250_power_tick(&p, true, 7001);
    assert(!p.outputs.power_button);
}

static void test_shutdown_timeout(void)
{
    bc250_power_logic_t p;
    bc250_power_timing_t timing = bc250_power_default_timing();
    timing.shutdown_timeout_ms = 1000;
    bc250_power_logic_init(&p, &timing, false, 0, true, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, true, 1));
    bc250_power_tick(&p, true, 1001);
    assert(p.state == BC250_POWER_FAULT);
    assert(!p.outputs.ps_on && !p.outputs.power_button);
}

static void test_ps_on_hold_detected_power(void)
{
    bc250_power_timing_t timing = bc250_power_default_timing();
    for (int enabled = 0; enabled <= 1; ++enabled) {
        bc250_power_logic_t p;
        /* Booting with an already-running board must not press its power button. */
        bc250_power_logic_init(&p, &timing, enabled, 0, true, 0);
        assert(p.state == BC250_POWER_ON);
        assert(p.outputs.ps_on == (bool)enabled && !p.outputs.power_button);
        assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, true, 1));
        bc250_power_tick(&p, true, 2);
        assert(p.outputs.ps_on == (bool)enabled && !p.outputs.power_button);
        bc250_power_tick(&p, false, 3);
        assert(p.state == BC250_POWER_OFF);
        assert(!p.outputs.ps_on && !p.outputs.power_button);

        /* External startup uses the same hold without a controller ON request. */
        bc250_power_tick(&p, true, 4);
        assert(p.state == BC250_POWER_ON);
        assert(p.outputs.ps_on == (bool)enabled && !p.outputs.power_button);
        bc250_power_tick(&p, false, 5);
        assert(!p.outputs.ps_on && !p.outputs.power_button);
    }
}

static void test_ps_on_hold_start_strategies(void)
{
    bc250_power_timing_t timing = bc250_power_default_timing();
    for (int strategy = BC250_START_PS_ON_ONLY; strategy <= BC250_START_SIMULTANEOUS; ++strategy) {
        bc250_power_logic_t p;
        timing.strategy = strategy;
        bc250_power_logic_init(&p, &timing, true, 0, false, 0);
        assert(!p.outputs.ps_on && !p.outputs.power_button);
        assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 1));
        assert(p.outputs.ps_on == (strategy != BC250_START_BUTTON_ONLY));
        bc250_power_tick(&p, true, 100);
        assert(p.outputs.ps_on && !p.outputs.power_button);
        bc250_power_tick(&p, true, 100 + timing.handoff_delay_ms);
        assert(p.state == BC250_POWER_ON);
        assert(p.outputs.ps_on && !p.outputs.power_button);
        bc250_power_tick(&p, true, 10000);
        assert(p.outputs.ps_on);
        bc250_power_tick(&p, false, 10001);
        assert(p.state == BC250_POWER_OFF && !p.outputs.ps_on);
    }
}

static void test_ps_on_hold_shutdown(void)
{
    bc250_power_timing_t timing = bc250_power_default_timing();
    const bc250_power_action_t actions[] = {
        BC250_POWER_ACTION_OFF, BC250_POWER_ACTION_TOGGLE, BC250_POWER_ACTION_FORCE_OFF,
    };
    for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        bc250_power_logic_t p;
        uint32_t pulse = actions[i] == BC250_POWER_ACTION_FORCE_OFF ? timing.force_off_ms : timing.button_pulse_ms;
        bc250_power_logic_init(&p, &timing, true, 0, true, 0);
        assert(bc250_power_request(&p, actions[i], true, 1));
        assert(p.state == BC250_POWER_STOPPING);
        assert(p.outputs.ps_on && p.outputs.power_button);
        bc250_power_tick(&p, true, pulse);
        assert(p.outputs.ps_on && p.outputs.power_button);
        bc250_power_tick(&p, true, pulse + 1);
        assert(p.outputs.ps_on && !p.outputs.power_button);
        bc250_power_tick(&p, false, pulse + 2);
        assert(p.state == BC250_POWER_OFF);
        assert(!p.outputs.ps_on && !p.outputs.power_button);
    }

    /* A failed graceful shutdown must not abruptly cut the still-running board. */
    bc250_power_logic_t p;
    bc250_power_logic_init(&p, &timing, true, 0, true, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, true, 1));
    bc250_power_tick(&p, true, 1 + timing.shutdown_timeout_ms);
    assert(p.state == BC250_POWER_FAULT);
    assert(p.outputs.ps_on && !p.outputs.power_button);
    bc250_power_tick(&p, true, 2 + timing.shutdown_timeout_ms);
    assert(p.outputs.ps_on && !p.outputs.power_button);
    bc250_power_tick(&p, false, 3 + timing.shutdown_timeout_ms);
    assert(!p.outputs.ps_on && !p.outputs.power_button);
}

static void test_ps_on_hold_start_interrupted(void)
{
    bc250_power_timing_t timing = bc250_power_default_timing();
    const bc250_power_action_t actions[] = {BC250_POWER_ACTION_OFF, BC250_POWER_ACTION_FORCE_OFF};
    for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        for (int sensed_on = 0; sensed_on <= 1; ++sensed_on) {
            bc250_power_logic_t p;
            bc250_power_logic_init(&p, &timing, true, 0, false, 0);
            assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 1));
            bc250_power_tick(&p, sensed_on, 2);
            assert(bc250_power_request(&p, actions[i], sensed_on, 3));
            assert(p.state == (sensed_on ? BC250_POWER_STOPPING : BC250_POWER_OFF));
            assert(p.outputs.ps_on == (bool)sensed_on);
            assert(p.outputs.power_button == (bool)sensed_on);
            bc250_power_tick(&p, false, 4);
            assert(!p.outputs.ps_on && !p.outputs.power_button);
        }
    }

    bc250_power_logic_t p;
    bc250_power_logic_init(&p, &timing, true, 0, false, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 1));
    bc250_power_tick(&p, false, 1 + timing.start_timeout_ms);
    assert(p.state == BC250_POWER_FAULT);
    assert(!p.outputs.ps_on && !p.outputs.power_button);
    /* A late, externally detected start should still be held. */
    bc250_power_tick(&p, true, 2 + timing.start_timeout_ms);
    assert(p.state == BC250_POWER_ON);
    assert(p.outputs.ps_on && !p.outputs.power_button);
}

static void test_ps_on_latched_without_sense(void)
{
    bc250_power_logic_t p;
    bc250_power_timing_t timing = bc250_power_default_timing();
    timing.strategy = BC250_START_PS_ON_LATCHED;
    bc250_power_logic_init(&p, &timing, true, 0, true, 0);
    assert(p.state == BC250_POWER_OFF && !p.outputs.ps_on && !p.outputs.power_button);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_TOGGLE, false, 1));
    assert(p.state == BC250_POWER_ON && p.outputs.ps_on && !p.outputs.power_button);
    bc250_power_tick(&p, false, timing.start_timeout_ms + 2);
    bc250_power_tick(&p, true, timing.start_timeout_ms + 3);
    assert(p.state == BC250_POWER_ON && p.outputs.ps_on);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 20000));
    assert(p.state == BC250_POWER_ON && p.outputs.ps_on);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_TOGGLE, false, 20001));
    assert(p.state == BC250_POWER_OFF && !p.outputs.ps_on && !p.outputs.power_button);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, true, 20002));
    assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, true, 20003));
    assert(p.state == BC250_POWER_OFF && !p.outputs.ps_on);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 20004));
    assert(bc250_power_request(&p, BC250_POWER_ACTION_FORCE_OFF, false, 20005));
    assert(p.state == BC250_POWER_OFF && !p.outputs.ps_on && !p.outputs.power_button);
}

static void test_delayed_power_off(void)
{
    bc250_power_timing_t timing = bc250_power_default_timing();
    timing.shutdown_timeout_ms = 1000;
    for (int strategy = BC250_START_PS_ON_ONLY; strategy <= BC250_START_PS_ON_LATCHED; ++strategy) {
        timing.strategy = strategy;
        bool latched = strategy == BC250_START_PS_ON_LATCHED;
        for (int hold = 0; hold <= 1; ++hold) {
            bc250_power_logic_t p;
            bc250_power_logic_init(&p, &timing, hold, 5000, true, 0);
            assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, true, 1));
            assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, true, 100));
            assert(p.state == BC250_POWER_OFF_PENDING);
            assert(!strcmp(bc250_power_state_name(p.state), "off_pending"));
            assert(p.outputs.ps_on == (latched || hold) && !p.outputs.power_button);
            bc250_power_tick(&p, true, 5099);
            assert(p.state == BC250_POWER_OFF_PENDING);
            assert(p.outputs.ps_on == (latched || hold) && !p.outputs.power_button);
            /* Repeated Off does not postpone shutdown. */
            assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, true, 5099));
            bc250_power_tick(&p, true, 5100);
            if (latched) {
                assert(p.state == BC250_POWER_OFF && !p.outputs.ps_on && !p.outputs.power_button);
            } else {
                assert(p.state == BC250_POWER_STOPPING && p.outputs.power_button);
                assert(p.outputs.ps_on == (bool)hold);
                /* Pulse and shutdown timeout start after the waiting period. */
                bc250_power_tick(&p, true, 5100 + timing.button_pulse_ms - 1);
                assert(p.outputs.power_button);
                bc250_power_tick(&p, true, 5100 + timing.button_pulse_ms);
                assert(p.state == BC250_POWER_STOPPING && !p.outputs.power_button);
                bc250_power_tick(&p, true, 6099);
                assert(p.state == BC250_POWER_STOPPING);
                bc250_power_tick(&p, true, 6100);
                assert(p.state == BC250_POWER_FAULT);
            }
        }
    }
}

static void test_cancel_delayed_power_off(void)
{
    bc250_power_timing_t timing = bc250_power_default_timing();
    const bc250_power_action_t cancel[] = {BC250_POWER_ACTION_ON, BC250_POWER_ACTION_TOGGLE};
    for (int strategy = BC250_START_PS_ON_ONLY; strategy <= BC250_START_PS_ON_LATCHED; ++strategy) {
        timing.strategy = strategy;
        bool latched = strategy == BC250_START_PS_ON_LATCHED;
        for (unsigned i = 0; i < sizeof(cancel) / sizeof(cancel[0]); ++i) {
            bc250_power_logic_t p;
            bool sensed = !latched;
            bc250_power_logic_init(&p, &timing, true, 5000, sensed, 0);
            assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, sensed, 1));
            assert(bc250_power_request(&p, BC250_POWER_ACTION_TOGGLE, sensed, 100));
            assert(p.state == BC250_POWER_OFF_PENDING && p.outputs.ps_on && !p.outputs.power_button);
            assert(!bc250_power_request(&p, BC250_POWER_ACTION_NONE, sensed, 101));
            assert(bc250_power_request(&p, cancel[i], sensed, 5099));
            assert(p.state == BC250_POWER_ON && p.outputs.ps_on && !p.outputs.power_button);
            bc250_power_tick(&p, sensed, 10000);
            assert(p.state == BC250_POWER_ON && p.outputs.ps_on && !p.outputs.power_button);
            /* A later Off gets its own full delay. */
            assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, sensed, 10001));
            bc250_power_tick(&p, sensed, 15000);
            assert(p.state == BC250_POWER_OFF_PENDING && !p.outputs.power_button);
            bc250_power_tick(&p, sensed, 15001);
            assert(p.state == (latched ? BC250_POWER_OFF : BC250_POWER_STOPPING));
        }
    }
}

static void test_delayed_power_off_edge_cases(void)
{
    bc250_power_timing_t timing = bc250_power_default_timing();
    bc250_power_logic_t p;
    for (int hold = 0; hold <= 1; ++hold) {
        for (int sensed = 0; sensed <= 1; ++sensed) {
            bc250_power_logic_init(&p, &timing, hold, 5000, false, 0);
            assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, false, 1));
            assert(p.state == BC250_POWER_OFF && !p.outputs.power_button);
            assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 2));
            assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, sensed, 3));
            assert(p.state == (sensed ? BC250_POWER_OFF_PENDING : BC250_POWER_OFF));
            assert(!p.outputs.power_button);
        }
    }
    for (int latched = 0; latched <= 1; ++latched) {
        timing.strategy = latched ? BC250_START_PS_ON_LATCHED : BC250_START_BUTTON_ONLY;
        bc250_power_logic_init(&p, &timing, true, 5000, !latched, 0);
        assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, !latched, 1));
        assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, !latched, 100));
        assert(bc250_power_request(&p, BC250_POWER_ACTION_FORCE_OFF, !latched, 101));
        assert(p.state == (latched ? BC250_POWER_OFF : BC250_POWER_STOPPING));
        assert(p.outputs.ps_on == !latched && p.outputs.power_button == !latched);
        if (!latched) {
            bc250_power_tick(&p, true, 101 + timing.force_off_ms - 1);
            assert(p.outputs.power_button);
            bc250_power_tick(&p, true, 101 + timing.force_off_ms);
            assert(!p.outputs.power_button);
        }
    }

    timing.strategy = BC250_START_BUTTON_ONLY;
    bc250_power_logic_init(&p, &timing, true, 5000, true, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, true, 100));
    bc250_power_tick(&p, false, 101);
    assert(p.state == BC250_POWER_OFF && !p.outputs.ps_on && !p.outputs.power_button);
    bc250_power_tick(&p, true, 200);
    bc250_power_tick(&p, true, 5100);
    assert(p.state == BC250_POWER_ON && p.outputs.ps_on && !p.outputs.power_button);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, true, 5200));
    /* An On at the deadline is too late to cancel a motherboard shutdown. */
    assert(!bc250_power_request(&p, BC250_POWER_ACTION_ON, true, 10200));
    assert(p.state == BC250_POWER_STOPPING && p.outputs.power_button);

    /* Long delays use the full uint32 range and a 64-bit clock. */
    timing.strategy = BC250_START_PS_ON_LATCHED;
    uint64_t start = (uint64_t)UINT32_MAX + 100;
    bc250_power_logic_init(&p, &timing, false, UINT32_MAX, false, start);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, start));
    assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, false, start));
    bc250_power_tick(&p, false, start + UINT32_MAX - 1);
    assert(p.state == BC250_POWER_OFF_PENDING && p.outputs.ps_on);
    bc250_power_tick(&p, false, start + UINT32_MAX);
    assert(p.state == BC250_POWER_OFF && !p.outputs.ps_on);
}

static void test_button_gestures(void)
{
    bc250_button_logic_t button;
    bc250_button_logic_init(&button, false, 0);
    assert(bc250_button_logic_update(&button, true, 10, 20, 100, 500) == BC250_GESTURE_NONE);
    assert(bc250_button_logic_update(&button, true, 30, 20, 100, 500) == BC250_GESTURE_NONE);
    assert(bc250_button_logic_update(&button, false, 40, 20, 100, 500) == BC250_GESTURE_NONE);
    assert(bc250_button_logic_update(&button, false, 60, 20, 100, 500) == BC250_GESTURE_NONE);
    assert(bc250_button_logic_update(&button, false, 160, 20, 100, 500) == BC250_GESTURE_SHORT);

    bc250_button_logic_init(&button, false, 0);
    bc250_button_logic_update(&button, true, 10, 20, 100, 500);
    bc250_button_logic_update(&button, true, 30, 20, 100, 500);
    bc250_button_logic_update(&button, false, 40, 20, 100, 500);
    bc250_button_logic_update(&button, false, 60, 20, 100, 500);
    bc250_button_logic_update(&button, true, 80, 20, 100, 500);
    bc250_button_logic_update(&button, true, 100, 20, 100, 500);
    bc250_button_logic_update(&button, false, 110, 20, 100, 500);
    bc250_button_logic_update(&button, false, 130, 20, 100, 500);
    assert(bc250_button_logic_update(&button, false, 230, 20, 100, 500) == BC250_GESTURE_DOUBLE);

    bc250_button_logic_init(&button, false, 0);
    bc250_button_logic_update(&button, true, 10, 20, 100, 500);
    bc250_button_logic_update(&button, true, 30, 20, 100, 500);
    assert(bc250_button_logic_update(&button, true, 530, 20, 100, 500) == BC250_GESTURE_LONG);
    assert(bc250_button_logic_update(&button, false, 550, 20, 100, 500) == BC250_GESTURE_NONE);
    assert(bc250_button_logic_update(&button, false, 700, 20, 100, 500) == BC250_GESTURE_NONE);
}

static void test_presence_deduplication(void)
{
    bc250_presence_logic_t presence;
    bc250_presence_logic_init(&presence);
    assert(bc250_presence_logic_seen(&presence, 100));
    assert(!bc250_presence_logic_seen(&presence, 200));
    assert(!bc250_presence_logic_expire(&presence, 30199, 30000));
    assert(bc250_presence_logic_expire(&presence, 30200, 30000));
    assert(!bc250_presence_logic_expire(&presence, 40000, 30000));
    assert(bc250_presence_logic_seen(&presence, 40001));
}

static void test_ble_matchers(void)
{
    const uint8_t adv[] = {
        8, 0x09, 'G', 'a', 'm', 'e', 'p', 'a', 'd',
        3, 0x03, 0x12, 0x18,
        6, 0xff, 0x4c, 0x00, 0x02, 0x15, 0xaa,
    };
    bc250_ble_advertisement_t packet = {
        .address = {0x66, 0x55, 0x44, 0x33, 0x22, 0x11},
        .rssi = -45,
        .data = adv,
        .data_length = sizeof(adv),
    };
    assert(bc250_ble_match_advertisement(0, "11:22:33:44:55:66", "", -80, &packet));
    assert(bc250_ble_match_advertisement(1, "Gamepad", "", -80, &packet));
    assert(bc250_ble_match_advertisement(2, "Game", "", -80, &packet));
    assert(bc250_ble_match_advertisement(3, "1812", "", -80, &packet));
    assert(bc250_ble_match_advertisement(4, "4c000200", "ffffff00", -80, &packet));
    assert(!bc250_ble_match_advertisement(1, "Other", "", -80, &packet));
    assert(!bc250_ble_match_advertisement(1, "Gamepad", "", -30, &packet));
}

int main(void)
{
    test_hp_commonslot_protocol();
    test_start_sequence();
    test_start_timeout();
    test_start_strategies_and_idempotence();
    test_zero_delay_and_fast_sense();
    test_conflicting_commands_deassert_outputs();
    test_graceful_and_force_off();
    test_shutdown_timeout();
    test_ps_on_hold_detected_power();
    test_ps_on_hold_start_strategies();
    test_ps_on_hold_shutdown();
    test_ps_on_hold_start_interrupted();
    test_ps_on_latched_without_sense();
    test_delayed_power_off();
    test_cancel_delayed_power_off();
    test_delayed_power_off_edge_cases();
    test_button_gestures();
    test_presence_deduplication();
    test_ble_matchers();
    return 0;
}
