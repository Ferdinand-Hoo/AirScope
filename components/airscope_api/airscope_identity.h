#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint8_t *certificate_pem;
    size_t certificate_len;
    uint8_t *private_key_pem;
    size_t private_key_len;
} airscope_https_identity_t;

esp_err_t airscope_identity_load_or_create(airscope_https_identity_t *identity);
void airscope_identity_free(airscope_https_identity_t *identity);
