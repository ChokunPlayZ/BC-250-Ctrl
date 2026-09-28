#include <assert.h>
#include <setjmp.h>
#include "mock_idf.h"
#include "status_led.h"
#include "gpio_service.h"
#include "power_service.h"

static TaskFunction_t led_task;
static jmp_buf task_done;
static bool samples[40];
static unsigned count, wanted;
static bc250_power_state_t power_state;

esp_err_t bc250_gpio_init_output(const bc250_output_config_t *config)
{
    assert(config->gpio == 6); return ESP_OK;
}
void bc250_gpio_write(const bc250_output_config_t *config, bool active)
{
    assert(config->gpio == 6);
    if (count < sizeof(samples) / sizeof(samples[0])) samples[count++] = active;
}
bc250_power_state_t bc250_power_service_state(void) { return power_state; }
int xTaskCreate(TaskFunction_t function, const char *name, unsigned stack, void *arg, unsigned priority, void *handle)
{
    (void)stack; (void)arg; (void)priority; (void)handle;
    assert(!strcmp(name, "status_led")); led_task = function; return pdPASS;
}
void vTaskDelay(unsigned ticks)
{
    assert(ticks == 125);
    if (count >= wanted) longjmp(task_done, 1);
}
static void sample(unsigned length)
{
    count = 0; wanted = length;
    if (!setjmp(task_done)) led_task(NULL);
    assert(count == length);
}
int main(void)
{
    bc250_config_t config = {.radio_profile = BC250_RADIO_ZIGBEE, .status_led = {.gpio = 6, .active_high = true}};
    assert(bc250_status_led_start(&config) == ESP_OK);
    bc250_status_led_set_zigbee_joining(true);
    const bc250_power_state_t states[] = {BC250_POWER_OFF, BC250_POWER_ON, BC250_POWER_STARTING,
                                         BC250_POWER_STOPPING, BC250_POWER_FAULT, BC250_POWER_UNKNOWN};
    for (unsigned state = 0; state < sizeof(states) / sizeof(states[0]); ++state) {
        power_state = states[state];
        sample(32);
        for (unsigned i = 0; i < 32; ++i) assert(samples[i] == (i % 8 == 0 || i % 8 == 2));
    }
    /* The configuration AP indication takes precedence over joining. */
    bc250_status_led_set_config_mode(true);
    power_state = BC250_POWER_ON;
    sample(32);
    for (unsigned i = 0; i < 32; ++i) assert(samples[i] == (i % 16 < 8));
    power_state = BC250_POWER_FAULT; /* AP pulses take precedence over every power state. */
    sample(32);
    for (unsigned i = 0; i < 32; ++i) assert(samples[i] == (i % 16 < 8));
    bc250_status_led_set_config_mode(false);
    sample(32);
    for (unsigned i = 0; i < 32; ++i) assert(samples[i] == (i % 8 == 0 || i % 8 == 2));
    bc250_status_led_set_zigbee_joining(false);
    sample(40);
    for (unsigned i = 0; i < 40; ++i) assert(samples[i] == (i % 20 == 0 || i % 20 == 2 || i % 20 == 4));
    power_state = BC250_POWER_ON; sample(16);
    for (unsigned i = 0; i < 16; ++i) assert(samples[i]);
    power_state = BC250_POWER_OFF; sample(16);
    for (unsigned i = 0; i < 16; ++i) assert(!samples[i]);
    bc250_status_led_set_config_mode(true);
    bc250_status_led_set_zigbee_joining(true);
    assert(bc250_status_led_start(&config) == ESP_OK); /* Boot resets both indications. */
    sample(16);
    for (unsigned i = 0; i < 16; ++i) assert(!samples[i]);
    config.radio_profile = BC250_RADIO_WIFI;
    assert(bc250_status_led_start(&config) == ESP_OK);
    bc250_status_led_set_zigbee_joining(true); sample(16);
    for (unsigned i = 0; i < 16; ++i) assert(!samples[i]);
    bc250_status_led_set_config_mode(true); sample(40);
    for (unsigned i = 0; i < 40; ++i) assert(samples[i] == (i % 20 == 0 || i % 20 == 2));
    puts("Zigbee joining, AP priority, power-state restoration, reboot and Wi-Fi LED patterns passed");
    return 0;
}
