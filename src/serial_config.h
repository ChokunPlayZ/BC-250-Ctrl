#pragma once

#include "config_store.h"

/* Edits a single typed setting. Full configuration validation happens on save. */
esp_err_t bc250_serial_config_set(bc250_config_t *config, const char *key,
                                  const char *value, char *error, size_t error_size);
void bc250_serial_config_print(const bc250_config_t *config);
/* Iterates editable setting names for completion; array slots are zero-based. */
bool bc250_serial_config_key(size_t index, char *key, size_t size);
