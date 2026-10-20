#pragma once

#include "config_store.h"

esp_err_t bc250_mqtt_service_start(const bc250_config_t *config);
bool bc250_mqtt_is_connected(void);
void bc250_mqtt_refresh(void);
