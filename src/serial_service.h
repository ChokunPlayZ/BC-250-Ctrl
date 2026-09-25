#pragma once

#include "esp_err.h"

/* Starts a line-oriented command task on the primary ESP-IDF console. */
esp_err_t bc250_serial_service_start(void);
