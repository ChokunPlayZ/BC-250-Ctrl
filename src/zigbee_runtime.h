#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* One worker owns initialization, the mainloop, and teardown across AP sessions. */
esp_err_t bc250_zigbee_runtime_start(void (*setup)(void), void (*teardown)(void));
/* Pause waits for teardown; resume starts again using the saved network data. */
esp_err_t bc250_zigbee_runtime_set_paused(bool paused);
