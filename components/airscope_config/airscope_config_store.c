#include "airscope_config_store.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

#define CONFIG_NAMESPACE "airscope_cfg"
#define CONFIG_KEY "ap_config"

static nvs_handle_t s_handle;
static bool s_initialized;

esp_err_t airscope_config_store_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    esp_err_t err = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &s_handle);
    if (err == ESP_OK) {
        s_initialized = true;
    }
    return err;
}

esp_err_t airscope_config_store_load(airscope_ap_config_t *config, bool *found)
{
    if (config == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = airscope_config_store_init();
    if (err != ESP_OK) {
        return err;
    }

    size_t size = sizeof(*config);
    err = nvs_get_blob(s_handle, CONFIG_KEY, config, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(config, 0, sizeof(*config));
        *found = false;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (size != sizeof(*config)) {
        *found = false;
        return ESP_ERR_INVALID_SIZE;
    }
    *found = true;
    return ESP_OK;
}

esp_err_t airscope_config_store_save(const airscope_ap_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = airscope_config_store_init();
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(s_handle, CONFIG_KEY, config, sizeof(*config));
    return err == ESP_OK ? nvs_commit(s_handle) : err;
}

esp_err_t airscope_config_store_erase(void)
{
    esp_err_t err = airscope_config_store_init();
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(s_handle, CONFIG_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    return err == ESP_OK ? nvs_commit(s_handle) : err;
}
