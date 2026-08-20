#pragma once

#include <stdbool.h>

#include "airscope_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *management_password;
    bool show_provisioning;
} airscope_display_start_options_t;

esp_err_t airscope_display_start(const airscope_ap_config_t *config,
                                 const airscope_display_start_options_t *options);
void airscope_display_show_initializing(const char *message);
void airscope_display_show_error(const char *stage, esp_err_t error);

#ifdef __cplusplus
}
#endif
