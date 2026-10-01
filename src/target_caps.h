#pragma once

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

/* Keep these in sync with the targets in tools/idf_build.py. */
#if defined(CONFIG_IDF_TARGET_ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32H21) || \
    defined(CONFIG_IDF_TARGET_ESP32H4)
#define BC250_HAS_WIFI 0
#else
#define BC250_HAS_WIFI 1
#endif

#if defined(CONFIG_IDF_TARGET_ESP32C5) || defined(CONFIG_IDF_TARGET_ESP32C6) || \
    defined(CONFIG_IDF_TARGET_ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32H21) || \
    defined(CONFIG_IDF_TARGET_ESP32H4)
#define BC250_HAS_ZIGBEE 1
#else
#define BC250_HAS_ZIGBEE 0
#endif

/* ESP32-S3 exposes GPIOs above 31; the configuration format uses int8_t. */
#ifdef ESP_PLATFORM
#include "soc/soc_caps.h"
#define BC250_GPIO_MAX (SOC_GPIO_PIN_COUNT - 1)
#else
#define BC250_GPIO_MAX 48
#endif
