#include <assert.h>
#include "../../../src/config_store.c"

static const void *saved_blob;
static size_t saved_size;
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *output, size_t *size)
{
    (void)handle; (void)key;
    assert(*size >= saved_size);
    memcpy(output, saved_blob, saved_size); *size = saved_size;
    return ESP_OK;
}
int main(void)
{
    bc250_config_t original, decoded;
    const size_t sizes[] = {0, offsetof(bc250_config_t, psu_i2c), offsetof(bc250_config_t, hold_ps_on),
        offsetof(bc250_config_t, power_off_delay_ms), offsetof(bc250_config_t, mqtt)};
    /* Fixture lengths are the historical 32-bit ESP layouts, including tail padding. */
    assert(sizes[1] == 3288 && sizes[2] == 3296 && sizes[3] == 3300 && sizes[4] == 3304);
    for (unsigned version = 1; version <= 4; ++version) {
        bc250_config_defaults(&original);
        strcpy(original.hostname, "saved-device");
        original.hold_ps_on = true;
        original.power_off_delay_ms = 4321;
        original.schema_version = version;
        original.crc32 = 0;
        original.crc32 = esp_crc32_le(0, (const uint8_t *)&original, sizes[version]);
        saved_blob = &original; saved_size = sizes[version];
        assert(read_blob(1, "active", &decoded) == ESP_OK);
        assert(config_blob_valid(&decoded));
        assert(!strcmp(decoded.hostname, "saved-device"));
        assert(decoded.hold_ps_on == (version >= 3));
        assert(decoded.power_off_delay_ms == (version >= 4 ? 4321U : 0U));
        assert(!decoded.mqtt.enabled && decoded.mqtt.port == 1883);
        assert(!strcmp(decoded.mqtt.discovery_prefix, "homeassistant"));
        assert(read_blob(1, "pending", &decoded) == ESP_OK);
        original.crc32 ^= 1;
        assert(read_blob(1, "active", &decoded) == ESP_ERR_INVALID_CRC);
    }
    char error[192];
    bc250_config_defaults(&original);
    assert(bc250_config_patch_json(&original,
        "{\"mqtt\":{\"enabled\":true,\"host\":\"192.168.1.10\",\"port\":1883,\"username\":\"bc250\",\"password\":\"broker-secret\"}}",
        error, sizeof(error)) == ESP_OK);
    char *json = bc250_config_to_json(&original, false);
    assert(json && !strstr(json, "broker-secret"));
    assert(bc250_config_patch_json(&original, json, error, sizeof(error)) == ESP_OK);
    assert(!strcmp(original.mqtt.password, "broker-secret"));
    cJSON_free(json);
    json = bc250_config_to_json(&original, true);
    assert(strstr(json, "broker-secret")); cJSON_free(json);
    assert(bc250_config_patch_json(&original, "{\"mqtt\":{\"clear_password\":true}}", error, sizeof(error)) == ESP_OK);
    assert(!original.mqtt.password[0]);
    const char *invalid[] = {"{\"mqtt\":true}", "{\"mqtt\":{\"enabled\":1}}", "{\"mqtt\":{\"port\":0}}",
        "{\"mqtt\":{\"port\":65536}}", "{\"mqtt\":{\"port\":1.5}}", "{\"mqtt\":{\"password\":false}}",
        "{\"mqtt\":{\"clear_password\":\"true\"}}", "{\"mqtt\":{\"host\":\"mqtt://broker\"}}",
        "{\"mqtt\":{\"discovery_prefix\":\"ha/#\"}}", "{\"mqtt\":{\"username\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        bc250_config_t next = original;
        assert(bc250_config_patch_json(&next, invalid[i], error, sizeof(error)) == ESP_ERR_INVALID_ARG);
    }
    original.crc32 = config_crc(&original);
    saved_blob = &original; saved_size = sizeof(original);
    assert(read_blob(1, "active", &decoded) == ESP_OK);
    assert(memcmp(&original, &decoded, sizeof(original)) == 0);
    puts("MQTT JSON, credential masking, CRC, and schema 1-4 migration tests passed");
}
