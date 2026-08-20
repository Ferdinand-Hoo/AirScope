#include "airscope_config.h"

#include <stdio.h>
#include <string.h>

static airscope_validation_result_t valid_result(void)
{
    return (airscope_validation_result_t){
        .valid = true,
        .field = NULL,
        .message = "",
    };
}

static airscope_validation_result_t invalid_result(const char *field, const char *message)
{
    airscope_validation_result_t result = {
        .valid = false,
        .field = field,
    };
    snprintf(result.message, sizeof(result.message), "%s", message);
    return result;
}

static bool protocol_supports_11n(airscope_protocol_t protocol)
{
    return protocol == AIRSCOPE_PROTOCOL_11BGN || protocol == AIRSCOPE_PROTOCOL_11GN;
}

static bool is_wpa3(airscope_auth_mode_t auth)
{
    return auth == AIRSCOPE_AUTH_WPA3_PSK || auth == AIRSCOPE_AUTH_WPA2_WPA3_PSK;
}

static bool is_supported_tx_power(int8_t power)
{
    static const int8_t supported[] = {8, 20, 28, 34, 44, 52, 56, 60, 66, 72, 80};
    for (size_t i = 0; i < sizeof(supported) / sizeof(supported[0]); ++i) {
        if (supported[i] == power) {
            return true;
        }
    }
    return false;
}

void airscope_config_make_default(airscope_ap_config_t *config, const uint8_t mac[6])
{
    if (config == NULL || mac == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->schema_version = AIRSCOPE_CONFIG_SCHEMA_VERSION;
    snprintf(config->ssid, sizeof(config->ssid), "AirScope-%02X%02X%02X", mac[3], mac[4],
             mac[5]);
    config->ssid_len = (uint8_t)strlen(config->ssid);
    config->auth_mode = AIRSCOPE_AUTH_OPEN;
    config->pairwise_cipher = AIRSCOPE_CIPHER_NONE;
    config->pmf = AIRSCOPE_PMF_DISABLED;
    config->sae_pwe = AIRSCOPE_SAE_BOTH;
    config->primary_channel = 1;
    config->bandwidth = AIRSCOPE_BANDWIDTH_HT20;
    config->secondary_channel = AIRSCOPE_SECONDARY_NONE;
    config->csa_count = 3;
    config->protocol = AIRSCOPE_PROTOCOL_11BGN;
    config->max_tx_power_quarter_dbm = 80;
    config->max_clients = 4;
    config->beacon_interval_tu = 100;
    config->dtim_period = 1;
}

airscope_validation_result_t airscope_config_validate(const airscope_ap_config_t *config,
                                                      bool allow_channel_change)
{
    if (config == NULL) {
        return invalid_result("config", "Configuration is required");
    }
    if (config->schema_version != AIRSCOPE_CONFIG_SCHEMA_VERSION) {
        return invalid_result("schemaVersion", "Unsupported configuration schema");
    }
    size_t ssid_len = strnlen(config->ssid, sizeof(config->ssid));
    if (ssid_len == 0 || ssid_len > AIRSCOPE_SSID_MAX_LEN || config->ssid_len != ssid_len) {
        return invalid_result("ssid", "SSID must contain 1 to 32 bytes");
    }
    size_t password_len = strnlen(config->ap_password, sizeof(config->ap_password));
    if (config->auth_mode == AIRSCOPE_AUTH_OPEN) {
        if (password_len != 0) {
            return invalid_result("apPassword", "Open networks must not have a password");
        }
        if (config->pairwise_cipher != AIRSCOPE_CIPHER_NONE) {
            return invalid_result("pairwiseCipher", "Open networks do not use a pairwise cipher");
        }
        if (config->pmf != AIRSCOPE_PMF_DISABLED) {
            return invalid_result("pmf", "Open networks do not use PMF");
        }
    } else if (password_len < 8 || password_len > AIRSCOPE_AP_PASSWORD_MAX_LEN) {
        return invalid_result("apPassword", "Password must contain 8 to 63 bytes");
    }

    switch (config->auth_mode) {
    case AIRSCOPE_AUTH_OPEN:
        break;
    case AIRSCOPE_AUTH_WPA_PSK:
        if (config->pairwise_cipher != AIRSCOPE_CIPHER_TKIP &&
            config->pairwise_cipher != AIRSCOPE_CIPHER_TKIP_CCMP) {
            return invalid_result("pairwiseCipher", "WPA requires TKIP or TKIP+CCMP");
        }
        break;
    case AIRSCOPE_AUTH_WPA2_PSK:
    case AIRSCOPE_AUTH_WPA_WPA2_PSK:
        if (config->pairwise_cipher != AIRSCOPE_CIPHER_TKIP &&
            config->pairwise_cipher != AIRSCOPE_CIPHER_CCMP &&
            config->pairwise_cipher != AIRSCOPE_CIPHER_TKIP_CCMP) {
            return invalid_result("pairwiseCipher", "WPA/WPA2 requires TKIP, CCMP, or both");
        }
        break;
    case AIRSCOPE_AUTH_WPA3_PSK:
    case AIRSCOPE_AUTH_WPA2_WPA3_PSK:
        if (config->pairwise_cipher != AIRSCOPE_CIPHER_CCMP &&
            config->pairwise_cipher != AIRSCOPE_CIPHER_GCMP &&
            config->pairwise_cipher != AIRSCOPE_CIPHER_GCMP256) {
            return invalid_result("pairwiseCipher", "WPA3 requires CCMP or GCMP");
        }
        if (config->pmf != AIRSCOPE_PMF_REQUIRED) {
            return invalid_result("pmf", "WPA3 requires PMF");
        }
        break;
    default:
        return invalid_result("authMode", "Unsupported authentication mode");
    }

    if (!is_wpa3(config->auth_mode) && config->sae_pwe != AIRSCOPE_SAE_BOTH) {
        return invalid_result("saePwe", "SAE settings apply only to WPA3 profiles");
    }
    if (config->primary_channel < 1 || config->primary_channel > 13) {
        return invalid_result("primaryChannel", "China 2.4 GHz channels are 1 through 13");
    }
    (void)allow_channel_change;
    if (config->bandwidth == AIRSCOPE_BANDWIDTH_HT20) {
        if (config->secondary_channel != AIRSCOPE_SECONDARY_NONE) {
            return invalid_result("secondaryChannel", "HT20 does not use a secondary channel");
        }
    } else if (config->bandwidth == AIRSCOPE_BANDWIDTH_HT40) {
        if (!protocol_supports_11n(config->protocol)) {
            return invalid_result("bandwidth", "HT40 requires an 802.11n protocol mode");
        }
        if (config->secondary_channel == AIRSCOPE_SECONDARY_ABOVE &&
            config->primary_channel > 9) {
            return invalid_result("secondaryChannel", "HT40 above requires primary channel 1 to 9");
        }
        if (config->secondary_channel == AIRSCOPE_SECONDARY_BELOW &&
            config->primary_channel < 5) {
            return invalid_result("secondaryChannel", "HT40 below requires primary channel 5 to 13");
        }
        if (config->secondary_channel == AIRSCOPE_SECONDARY_NONE) {
            return invalid_result("secondaryChannel", "HT40 requires a secondary direction");
        }
    } else {
        return invalid_result("bandwidth", "Unsupported bandwidth");
    }
    if (!is_supported_tx_power(config->max_tx_power_quarter_dbm)) {
        return invalid_result("maxTxPowerQuarterDbm", "Unsupported transmit-power step");
    }
    if (config->max_clients < 1 || config->max_clients > 10) {
        return invalid_result("maxClients", "Client limit must be between 1 and 10");
    }
    if (config->beacon_interval_tu < 100 || config->beacon_interval_tu > 60000 ||
        config->beacon_interval_tu % 100 != 0) {
        return invalid_result("beaconIntervalTu",
                              "Beacon interval must be 100 to 60000 TU in steps of 100");
    }
    if (config->dtim_period < 1 || config->dtim_period > 10) {
        return invalid_result("dtimPeriod", "DTIM period must be between 1 and 10");
    }
    if (config->csa_count < 1 || config->csa_count > 15) {
        return invalid_result("csaCount", "CSA count must be between 1 and 15");
    }
    return valid_result();
}

bool airscope_config_equal(const airscope_ap_config_t *left, const airscope_ap_config_t *right)
{
    return left != NULL && right != NULL &&
           left->schema_version == right->schema_version &&
           left->ssid_len == right->ssid_len &&
           memcmp(left->ssid, right->ssid, left->ssid_len) == 0 &&
           left->ssid_hidden == right->ssid_hidden &&
           strcmp(left->ap_password, right->ap_password) == 0 &&
           left->auth_mode == right->auth_mode &&
           left->pairwise_cipher == right->pairwise_cipher &&
           left->pmf == right->pmf &&
           left->sae_pwe == right->sae_pwe &&
           left->primary_channel == right->primary_channel &&
           left->bandwidth == right->bandwidth &&
           left->secondary_channel == right->secondary_channel &&
           left->csa_count == right->csa_count &&
           left->protocol == right->protocol &&
           left->max_tx_power_quarter_dbm == right->max_tx_power_quarter_dbm &&
           left->max_clients == right->max_clients &&
           left->beacon_interval_tu == right->beacon_interval_tu &&
           left->dtim_period == right->dtim_period;
}

bool airscope_config_non_channel_equal(const airscope_ap_config_t *left,
                                       const airscope_ap_config_t *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    return left->schema_version == right->schema_version &&
           left->ssid_len == right->ssid_len &&
           memcmp(left->ssid, right->ssid, left->ssid_len) == 0 &&
           left->ssid_hidden == right->ssid_hidden &&
           strcmp(left->ap_password, right->ap_password) == 0 &&
           left->auth_mode == right->auth_mode &&
           left->pairwise_cipher == right->pairwise_cipher &&
           left->pmf == right->pmf &&
           left->sae_pwe == right->sae_pwe &&
           left->protocol == right->protocol &&
           left->max_tx_power_quarter_dbm == right->max_tx_power_quarter_dbm &&
           left->max_clients == right->max_clients &&
           left->beacon_interval_tu == right->beacon_interval_tu &&
           left->dtim_period == right->dtim_period;
}

#define DEFINE_ENUM_NAME(fn, type, ...)                                      \
    const char *fn(type value)                                               \
    {                                                                        \
        static const char *const names[] = {__VA_ARGS__};                    \
        return (unsigned)value < sizeof(names) / sizeof(names[0])            \
                   ? names[value]                                            \
                   : "unknown";                                              \
    }

DEFINE_ENUM_NAME(airscope_auth_mode_name, airscope_auth_mode_t,
                 "open", "wpa-psk", "wpa2-psk", "wpa-wpa2-psk", "wpa3-psk",
                 "wpa2-wpa3-psk")
DEFINE_ENUM_NAME(airscope_cipher_name, airscope_cipher_t,
                 "none", "tkip", "ccmp", "tkip-ccmp", "gcmp", "gcmp256")
DEFINE_ENUM_NAME(airscope_pmf_name, airscope_pmf_mode_t,
                 "disabled", "optional", "required")
DEFINE_ENUM_NAME(airscope_sae_pwe_name, airscope_sae_pwe_t,
                 "hunt-and-peck", "hash-to-element", "both")
DEFINE_ENUM_NAME(airscope_protocol_name, airscope_protocol_t, "b", "bg", "bgn", "gn")
DEFINE_ENUM_NAME(airscope_bandwidth_name, airscope_bandwidth_t, "ht20", "ht40")
DEFINE_ENUM_NAME(airscope_secondary_channel_name, airscope_secondary_channel_t,
                 "none", "above", "below")

#define DEFINE_ENUM_PARSE(fn, type, name_fn, max_value) \
    bool fn(const char *value, type *out)               \
    {                                                    \
        if (value == NULL || out == NULL) {              \
            return false;                                \
        }                                                \
        for (int i = 0; i <= (max_value); ++i) {         \
            if (strcmp(value, name_fn((type)i)) == 0) {  \
                *out = (type)i;                          \
                return true;                             \
            }                                            \
        }                                                \
        return false;                                    \
    }

DEFINE_ENUM_PARSE(airscope_config_parse_auth_mode, airscope_auth_mode_t,
                  airscope_auth_mode_name, AIRSCOPE_AUTH_WPA2_WPA3_PSK)
DEFINE_ENUM_PARSE(airscope_config_parse_cipher, airscope_cipher_t, airscope_cipher_name,
                  AIRSCOPE_CIPHER_GCMP256)
DEFINE_ENUM_PARSE(airscope_config_parse_pmf, airscope_pmf_mode_t, airscope_pmf_name,
                  AIRSCOPE_PMF_REQUIRED)
DEFINE_ENUM_PARSE(airscope_config_parse_sae_pwe, airscope_sae_pwe_t, airscope_sae_pwe_name,
                  AIRSCOPE_SAE_BOTH)
DEFINE_ENUM_PARSE(airscope_config_parse_protocol, airscope_protocol_t, airscope_protocol_name,
                  AIRSCOPE_PROTOCOL_11GN)
DEFINE_ENUM_PARSE(airscope_config_parse_bandwidth, airscope_bandwidth_t,
                  airscope_bandwidth_name, AIRSCOPE_BANDWIDTH_HT40)
DEFINE_ENUM_PARSE(airscope_config_parse_secondary_channel, airscope_secondary_channel_t,
                  airscope_secondary_channel_name, AIRSCOPE_SECONDARY_BELOW)
