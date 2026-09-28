#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_partition.h"

void app_main(void);

static const esp_partition_t factory_partition = {.size = 0x1000};
static esp_err_t nvs_result, factory_result;
static bool missing_factory;
static int nvs_erases, lookups, factory_erases;
static char messages[1024];

void recovery_test_log(const char *format, ...)
{
    size_t used = strlen(messages);
    va_list args;
    va_start(args, format);
    vsnprintf(messages + used, sizeof(messages) - used, format, args);
    va_end(args);
}

const char *esp_err_to_name(esp_err_t error)
{
    (void)error;
    return "test erase error";
}

esp_err_t nvs_flash_erase(void)
{
    nvs_erases++;
    return nvs_result;
}

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label)
{
    assert(nvs_erases > 0);
    assert(type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_ANY);
    assert(strcmp(label, "zb_fct") == 0);
    lookups++;
    return missing_factory ? NULL : &factory_partition;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size)
{
    assert(partition == &factory_partition);
    assert(offset == 0 && size == factory_partition.size);
    factory_erases++;
    return factory_result;
}

static void reset(void)
{
    nvs_result = factory_result = ESP_OK;
    missing_factory = false;
    nvs_erases = lookups = factory_erases = 0;
    messages[0] = '\0';
}

int main(void)
{
    reset();
    app_main();
    assert(nvs_erases == 1 && lookups == 1 && factory_erases == 1);
    assert(strstr(messages, "RECOVERY COMPLETE"));
    messages[0] = '\0';
    app_main();
    assert(nvs_erases == 2 && factory_erases == 2);
    assert(strstr(messages, "RECOVERY COMPLETE"));

    reset();
    nvs_result = ESP_ERR_INVALID_ARG;
    app_main();
    assert(nvs_erases == 1 && lookups == 0 && factory_erases == 0);
    assert(strstr(messages, "NVS erase failed"));
    assert(!strstr(messages, "RECOVERY COMPLETE"));

    reset();
    missing_factory = true;
    app_main();
    assert(nvs_erases == 1 && lookups == 1 && factory_erases == 0);
    assert(strstr(messages, "recovery incomplete"));
    assert(!strstr(messages, "RECOVERY COMPLETE"));

    reset();
    factory_result = ESP_ERR_INVALID_ARG;
    app_main();
    assert(nvs_erases == 1 && factory_erases == 1);
    assert(strstr(messages, "Zigbee factory erase failed"));
    assert(!strstr(messages, "RECOVERY COMPLETE"));
    puts("Recovery erase and failure handling tests passed");
    return 0;
}
