#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIRSCOPE_CONFIG_SCHEMA_VERSION 2
#define AIRSCOPE_SSID_MAX_LEN 32
#define AIRSCOPE_AP_PASSWORD_MAX_LEN 63
#define AIRSCOPE_VALIDATION_MESSAGE_MAX_LEN 96

typedef enum {
    AIRSCOPE_AUTH_OPEN = 0,
    AIRSCOPE_AUTH_WPA_PSK,
    AIRSCOPE_AUTH_WPA2_PSK,
    AIRSCOPE_AUTH_WPA_WPA2_PSK,
    AIRSCOPE_AUTH_WPA3_PSK,
    AIRSCOPE_AUTH_WPA2_WPA3_PSK,
} airscope_auth_mode_t;

typedef enum {
    AIRSCOPE_CIPHER_NONE = 0,
    AIRSCOPE_CIPHER_TKIP,
    AIRSCOPE_CIPHER_CCMP,
    AIRSCOPE_CIPHER_TKIP_CCMP,
    AIRSCOPE_CIPHER_GCMP,
    AIRSCOPE_CIPHER_GCMP256,
} airscope_cipher_t;

typedef enum {
    AIRSCOPE_PMF_DISABLED = 0,
    AIRSCOPE_PMF_OPTIONAL,
    AIRSCOPE_PMF_REQUIRED,
} airscope_pmf_mode_t;

typedef enum {
    AIRSCOPE_SAE_HUNT_AND_PECK = 0,
    AIRSCOPE_SAE_HASH_TO_ELEMENT,
    AIRSCOPE_SAE_BOTH,
} airscope_sae_pwe_t;

typedef enum {
    AIRSCOPE_PROTOCOL_11B = 0,
    AIRSCOPE_PROTOCOL_11BG,
    AIRSCOPE_PROTOCOL_11BGN,
    AIRSCOPE_PROTOCOL_11GN,
} airscope_protocol_t;

typedef enum {
    AIRSCOPE_BANDWIDTH_HT20 = 0,
    AIRSCOPE_BANDWIDTH_HT40,
} airscope_bandwidth_t;

typedef enum {
    AIRSCOPE_SECONDARY_NONE = 0,
    AIRSCOPE_SECONDARY_ABOVE,
    AIRSCOPE_SECONDARY_BELOW,
} airscope_secondary_channel_t;

typedef struct {
    uint32_t schema_version;
    char ssid[AIRSCOPE_SSID_MAX_LEN + 1];
    uint8_t ssid_len;
    bool ssid_hidden;
    char ap_password[AIRSCOPE_AP_PASSWORD_MAX_LEN + 1];
    airscope_auth_mode_t auth_mode;
    airscope_cipher_t pairwise_cipher;
    airscope_pmf_mode_t pmf;
    airscope_sae_pwe_t sae_pwe;
    uint8_t primary_channel;
    airscope_bandwidth_t bandwidth;
    airscope_secondary_channel_t secondary_channel;
    uint8_t csa_count;
    airscope_protocol_t protocol;
    int8_t max_tx_power_quarter_dbm;
    uint8_t max_clients;
    uint16_t beacon_interval_tu;
    uint8_t dtim_period;
} airscope_ap_config_t;

typedef struct {
    bool valid;
    const char *field;
    char message[AIRSCOPE_VALIDATION_MESSAGE_MAX_LEN];
} airscope_validation_result_t;

void airscope_config_make_default(airscope_ap_config_t *config, const uint8_t mac[6]);
airscope_validation_result_t airscope_config_validate(const airscope_ap_config_t *config,
                                                      bool allow_channel_change);
bool airscope_config_equal(const airscope_ap_config_t *left, const airscope_ap_config_t *right);
bool airscope_config_non_channel_equal(const airscope_ap_config_t *left,
                                       const airscope_ap_config_t *right);
const char *airscope_auth_mode_name(airscope_auth_mode_t value);
const char *airscope_cipher_name(airscope_cipher_t value);
const char *airscope_pmf_name(airscope_pmf_mode_t value);
const char *airscope_sae_pwe_name(airscope_sae_pwe_t value);
const char *airscope_protocol_name(airscope_protocol_t value);
const char *airscope_bandwidth_name(airscope_bandwidth_t value);
const char *airscope_secondary_channel_name(airscope_secondary_channel_t value);
bool airscope_config_parse_auth_mode(const char *value, airscope_auth_mode_t *out);
bool airscope_config_parse_cipher(const char *value, airscope_cipher_t *out);
bool airscope_config_parse_pmf(const char *value, airscope_pmf_mode_t *out);
bool airscope_config_parse_sae_pwe(const char *value, airscope_sae_pwe_t *out);
bool airscope_config_parse_protocol(const char *value, airscope_protocol_t *out);
bool airscope_config_parse_bandwidth(const char *value, airscope_bandwidth_t *out);
bool airscope_config_parse_secondary_channel(const char *value,
                                             airscope_secondary_channel_t *out);

#ifdef __cplusplus
}
#endif
