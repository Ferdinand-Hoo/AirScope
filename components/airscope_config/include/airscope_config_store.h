#pragma once

#include <stdbool.h>

#include "airscope_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t airscope_config_store_init(void);
esp_err_t airscope_config_store_load(airscope_ap_config_t *config, bool *found);
esp_err_t airscope_config_store_save(const airscope_ap_config_t *config);
esp_err_t airscope_config_store_erase(void);

#ifdef __cplusplus
}
#endif
