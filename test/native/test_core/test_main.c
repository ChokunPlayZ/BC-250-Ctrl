#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "../../../src/core/power_logic.h"
#include "../../../src/core/ble_match.h"

static void test_start_sequence(void)
{
    bc250_power_logic_t p;
    bc250_power_timing_t timing = bc250_power_default_timing();
    bc250_power_logic_init(&p, &timing, false, 0);
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
    bc250_power_logic_init(&p, &timing, false, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 1));
    bc250_power_tick(&p, false, 15001);
    assert(p.state == BC250_POWER_FAULT);
    assert(!p.outputs.ps_on && !p.outputs.power_button);
    assert(!bc250_power_request(&p, BC250_POWER_ACTION_ON, false, 15002));
}

static void test_graceful_and_force_off(void)
{
    bc250_power_logic_t p;
    bc250_power_timing_t timing = bc250_power_default_timing();
    bc250_power_logic_init(&p, &timing, true, 0);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_OFF, true, 1));
    assert(p.outputs.power_button);
    bc250_power_tick(&p, true, 251);
    assert(!p.outputs.power_button);
    bc250_power_tick(&p, false, 1000);
    assert(p.state == BC250_POWER_OFF);

    bc250_power_logic_init(&p, &timing, true, 2000);
    assert(bc250_power_request(&p, BC250_POWER_ACTION_FORCE_OFF, true, 2001));
    bc250_power_tick(&p, true, 6999);
    assert(p.outputs.power_button);
    bc250_power_tick(&p, true, 7001);
    assert(!p.outputs.power_button);
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
    test_start_sequence();
    test_start_timeout();
    test_graceful_and_force_off();
    test_ble_matchers();
    puts("core tests passed");
    return 0;
}
