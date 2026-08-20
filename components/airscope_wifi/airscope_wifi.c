#include "airscope_wifi.h"

#include <stdio.h>
#include <string.h>

#include "airscope_config_store.h"
#include "airscope_events.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "apps/dhcpserver/dhcpserver.h"

static const char *TAG = "airscope_wifi";
static airscope_ap_config_t s_applied;
static airscope_operation_result_t s_latest_operation;
static bool s_started;
static bool s_degraded;
static SemaphoreHandle_t s_mutex;

static wifi_auth_mode_t map_auth(airscope_auth_mode_t value)
{
    static const wifi_auth_mode_t values[] = {
        WIFI_AUTH_OPEN,       WIFI_AUTH_WPA_PSK,      WIFI_AUTH_WPA2_PSK,
        WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK,
    };
    return values[value];
}

static wifi_cipher_type_t map_cipher(airscope_cipher_t value)
{
    static const wifi_cipher_type_t values[] = {
        WIFI_CIPHER_TYPE_NONE,      WIFI_CIPHER_TYPE_TKIP,
        WIFI_CIPHER_TYPE_CCMP,      WIFI_CIPHER_TYPE_TKIP_CCMP,
        WIFI_CIPHER_TYPE_GCMP,      WIFI_CIPHER_TYPE_GCMP256,
    };
    return values[value];
}

static wifi_second_chan_t map_secondary(airscope_secondary_channel_t value)
{
    static const wifi_second_chan_t values[] = {
        WIFI_SECOND_CHAN_NONE,
        WIFI_SECOND_CHAN_ABOVE,
        WIFI_SECOND_CHAN_BELOW,
    };
    return values[value];
}

static uint8_t map_protocol(airscope_protocol_t value)
{
    switch (value) {
    case AIRSCOPE_PROTOCOL_11B:
        return WIFI_PROTOCOL_11B;
    case AIRSCOPE_PROTOCOL_11BG:
        return WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G;
    case AIRSCOPE_PROTOCOL_11BGN:
        return WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
    case AIRSCOPE_PROTOCOL_11GN:
        return WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
    default:
        return 0;
    }
}

static void make_operation_id(char out[AIRSCOPE_OPERATION_ID_LEN + 1])
{
    static const char hex[] = "0123456789abcdef";
    uint8_t random[AIRSCOPE_OPERATION_ID_LEN / 2];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) {
        out[i * 2] = hex[random[i] >> 4];
        out[i * 2 + 1] = hex[random[i] & 0x0f];
    }
    out[AIRSCOPE_OPERATION_ID_LEN] = '\0';
}

static airscope_operation_result_t operation_start(airscope_operation_type_t type)
{
    airscope_operation_result_t result = {
        .type = type,
        .success = false,
        .error = ESP_FAIL,
    };
    make_operation_id(result.id);
    return result;
}

static void operation_fail(airscope_operation_result_t *result, esp_err_t error,
                           const char *message)
{
    result->success = false;
    result->error = error;
    snprintf(result->message, sizeof(result->message), "%s", message);
}

static void operation_succeed(airscope_operation_result_t *result, const char *message)
{
    result->success = true;
    result->error = ESP_OK;
    snprintf(result->message, sizeof(result->message), "%s", message);
}

static wifi_config_t make_wifi_config(const airscope_ap_config_t *config)
{
    wifi_config_t output = {0};
    memcpy(output.ap.ssid, config->ssid, config->ssid_len);
    output.ap.ssid_len = config->ssid_len;
    memcpy(output.ap.password, config->ap_password, strlen(config->ap_password));
    output.ap.channel = config->primary_channel;
    output.ap.authmode = map_auth(config->auth_mode);
    output.ap.ssid_hidden = config->ssid_hidden;
    output.ap.max_connection = config->max_clients;
    output.ap.beacon_interval = config->beacon_interval_tu;
    output.ap.csa_count = config->csa_count;
    output.ap.dtim_period = config->dtim_period;
    output.ap.pairwise_cipher = map_cipher(config->pairwise_cipher);
    output.ap.pmf_cfg.capable = config->pmf != AIRSCOPE_PMF_DISABLED;
    output.ap.pmf_cfg.required = config->pmf == AIRSCOPE_PMF_REQUIRED;
    if (config->auth_mode == AIRSCOPE_AUTH_WPA3_PSK ||
        config->auth_mode == AIRSCOPE_AUTH_WPA2_WPA3_PSK) {
        output.ap.sae_pwe_h2e = config->sae_pwe == AIRSCOPE_SAE_HUNT_AND_PECK
                                    ? WPA3_SAE_PWE_HUNT_AND_PECK
                                : config->sae_pwe == AIRSCOPE_SAE_HASH_TO_ELEMENT
                                    ? WPA3_SAE_PWE_HASH_TO_ELEMENT
                                    : WPA3_SAE_PWE_BOTH;
    }
    return output;
}

static bool config_requires_restart(const airscope_ap_config_t *current,
                                    const airscope_ap_config_t *proposed)
{
    return current->ssid_len != proposed->ssid_len ||
           memcmp(current->ssid, proposed->ssid, current->ssid_len) != 0 ||
           current->ssid_hidden != proposed->ssid_hidden ||
           strcmp(current->ap_password, proposed->ap_password) != 0 ||
           current->auth_mode != proposed->auth_mode ||
           current->pairwise_cipher != proposed->pairwise_cipher ||
           current->pmf != proposed->pmf ||
           current->sae_pwe != proposed->sae_pwe ||
           current->protocol != proposed->protocol;
}

static esp_err_t apply_runtime(const airscope_ap_config_t *config, bool restart)
{
    wifi_config_t wifi_config = make_wifi_config(config);
    esp_err_t err = ESP_OK;
    if (restart && s_started) {
        err = esp_wifi_stop();
        if (err == ESP_OK) {
            s_started = false;
        }
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    }
    if (err == ESP_OK && restart) {
        err = esp_wifi_start();
        if (err == ESP_OK) {
            s_started = true;
        }
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_protocol(WIFI_IF_AP, map_protocol(config->protocol));
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_bandwidth(
            WIFI_IF_AP, config->bandwidth == AIRSCOPE_BANDWIDTH_HT40 ? WIFI_BW40
                                                                     : WIFI_BW20);
    }
    if (err == ESP_OK && s_started) {
        err = esp_wifi_set_channel(config->primary_channel,
                                   map_secondary(config->secondary_channel));
    }
    if (err == ESP_OK && s_started) {
        err = esp_wifi_set_max_tx_power(config->max_tx_power_quarter_dbm);
    }
    return err;
}

static esp_err_t verify_runtime_once(const airscope_ap_config_t *expected,
                                     bool log_mismatch)
{
    wifi_config_t actual = {0};
    uint8_t protocol = 0;
    wifi_bandwidth_t bandwidth;
    uint8_t channel = 0;
    wifi_second_chan_t secondary;
    int8_t power = 0;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_AP, &actual);
    if (err == ESP_OK) {
        err = esp_wifi_get_protocol(WIFI_IF_AP, &protocol);
    }
    if (err == ESP_OK) {
        err = esp_wifi_get_bandwidth(WIFI_IF_AP, &bandwidth);
    }
    if (err == ESP_OK && s_started) {
        err = esp_wifi_get_channel(&channel, &secondary);
    }
    if (err == ESP_OK && s_started) {
        err = esp_wifi_get_max_tx_power(&power);
    }
    if (err != ESP_OK) {
        return err;
    }
    bool mismatch = false;
#define VERIFY_FIELD(name, actual_value, expected_value)                                  \
    do {                                                                                  \
        long actual_log_value = (long)(actual_value);                                     \
        long expected_log_value = (long)(expected_value);                                 \
        if (actual_log_value != expected_log_value) {                                     \
            if (log_mismatch) {                                                           \
                ESP_LOGE(TAG, "runtime mismatch: %s actual=%ld expected=%ld", name,       \
                         actual_log_value, expected_log_value);                           \
            }                                                                             \
            mismatch = true;                                                              \
        }                                                                                 \
    } while (0)

    VERIFY_FIELD("ssid_len", actual.ap.ssid_len, expected->ssid_len);
    if (actual.ap.ssid_len != expected->ssid_len ||
        memcmp(actual.ap.ssid, expected->ssid, expected->ssid_len) != 0) {
        if (log_mismatch) {
            ESP_LOGE(TAG, "runtime mismatch: ssid actual='%.*s' expected='%.*s'",
                     actual.ap.ssid_len, (const char *)actual.ap.ssid, expected->ssid_len,
                     expected->ssid);
        }
        mismatch = true;
    }
    VERIFY_FIELD("authmode", actual.ap.authmode, map_auth(expected->auth_mode));
    VERIFY_FIELD("pairwise_cipher", actual.ap.pairwise_cipher,
                 map_cipher(expected->pairwise_cipher));
    VERIFY_FIELD("pmf_capable", actual.ap.pmf_cfg.capable,
                 expected->pmf != AIRSCOPE_PMF_DISABLED);
    VERIFY_FIELD("pmf_required", actual.ap.pmf_cfg.required,
                 expected->pmf == AIRSCOPE_PMF_REQUIRED);
    if (strncmp((const char *)actual.ap.password, expected->ap_password,
                sizeof(actual.ap.password)) != 0) {
        if (log_mismatch) {
            ESP_LOGE(TAG, "runtime mismatch: AP password differs");
        }
        mismatch = true;
    }
    VERIFY_FIELD("max_connection", actual.ap.max_connection, expected->max_clients);
    VERIFY_FIELD("beacon_interval", actual.ap.beacon_interval,
                 expected->beacon_interval_tu);
    VERIFY_FIELD("dtim_period", actual.ap.dtim_period, expected->dtim_period);
    VERIFY_FIELD("protocol", protocol, map_protocol(expected->protocol));
    VERIFY_FIELD("bandwidth", bandwidth,
                 expected->bandwidth == AIRSCOPE_BANDWIDTH_HT40 ? WIFI_BW40 : WIFI_BW20);
    if (s_started) {
        VERIFY_FIELD("channel", channel, expected->primary_channel);
        VERIFY_FIELD("secondary_channel", secondary,
                     map_secondary(expected->secondary_channel));
        VERIFY_FIELD("max_tx_power", power, expected->max_tx_power_quarter_dbm);
    }
#undef VERIFY_FIELD

    if (mismatch) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t verify_runtime(const airscope_ap_config_t *expected)
{
    const uint8_t max_attempts = 5;
    for (uint8_t attempt = 1; attempt <= max_attempts; ++attempt) {
        esp_err_t err = verify_runtime_once(expected, attempt == max_attempts);
        if (err == ESP_OK || err != ESP_ERR_INVALID_RESPONSE) {
            return err;
        }
        if (attempt < max_attempts) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t configure_management_network(void)
{
    esp_netif_t *netif = esp_netif_create_default_wifi_ap();
    return netif != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t airscope_wifi_init(const airscope_ap_config_t *initial_config)
{
    if (initial_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    airscope_validation_result_t validation =
        airscope_config_validate(initial_config, true);
    if (!validation.valid) {
        ESP_LOGE(TAG, "Initial configuration invalid: %s", validation.message);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(configure_management_network(), TAG,
                        "management network configuration failed");
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init), TAG, "WiFi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG,
                        "WiFi RAM storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "AP mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_country_code("CN", false), TAG,
                        "country configuration failed");
    wifi_config_t wifi_config = make_wifi_config(initial_config);
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG,
                        "initial AP configuration failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "WiFi start failed");
    s_started = true;
    ESP_RETURN_ON_ERROR(
        esp_wifi_set_protocol(WIFI_IF_AP, map_protocol(initial_config->protocol)),
        TAG, "protocol configuration failed");
    ESP_RETURN_ON_ERROR(
        esp_wifi_set_bandwidth(
            WIFI_IF_AP, initial_config->bandwidth == AIRSCOPE_BANDWIDTH_HT40
                              ? WIFI_BW40
                              : WIFI_BW20),
        TAG, "bandwidth configuration failed");
    ESP_RETURN_ON_ERROR(
        esp_wifi_set_channel(initial_config->primary_channel,
                             map_secondary(initial_config->secondary_channel)),
        TAG, "channel configuration failed");
    ESP_RETURN_ON_ERROR(
        esp_wifi_set_max_tx_power(initial_config->max_tx_power_quarter_dbm),
        TAG, "transmit power configuration failed");
    ESP_RETURN_ON_ERROR(verify_runtime(initial_config), TAG, "runtime readback failed");
    s_applied = *initial_config;
    ESP_LOGI(TAG,
             "SoftAP active: SSID='%s' hidden=%s channel=%u auth=%s bandwidth=%s",
             initial_config->ssid, initial_config->ssid_hidden ? "yes" : "no",
             initial_config->primary_channel,
             airscope_auth_mode_name(initial_config->auth_mode),
             airscope_bandwidth_name(initial_config->bandwidth));
    airscope_events_record(AIRSCOPE_EVENT_INFO, "ap.started",
                           "{\"managementAddress\":\"192.168.4.1\"}");
    return ESP_OK;
}

bool airscope_wifi_is_started(void)
{
    return s_started;
}

bool airscope_wifi_is_degraded(void)
{
    return s_degraded;
}

void airscope_wifi_get_applied_config(airscope_ap_config_t *out)
{
    if (out == NULL || s_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_applied;
    xSemaphoreGive(s_mutex);
}

airscope_operation_result_t airscope_wifi_apply_config(
    const airscope_ap_config_t *proposed)
{
    airscope_operation_result_t result =
        operation_start(AIRSCOPE_OPERATION_CONFIG_TRANSACTION);
    if (proposed == NULL) {
        operation_fail(&result, ESP_ERR_INVALID_ARG, "Configuration is required");
        return result;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    airscope_ap_config_t previous = s_applied;
    airscope_validation_result_t validation = airscope_config_validate(proposed, true);
    if (!validation.valid) {
        operation_fail(&result, ESP_ERR_INVALID_ARG, validation.message);
        goto done;
    }
    if (proposed->primary_channel != previous.primary_channel ||
        proposed->bandwidth != previous.bandwidth ||
        proposed->secondary_channel != previous.secondary_channel ||
        proposed->csa_count != previous.csa_count) {
        operation_fail(&result, ESP_ERR_INVALID_ARG,
                       "Channel fields require a Channel Switch Operation");
        goto done;
    }

    bool restart = config_requires_restart(&previous, proposed);
    esp_err_t err = apply_runtime(proposed, restart);
    if (err == ESP_OK) {
        err = verify_runtime(proposed);
    }
    if (err == ESP_OK) {
        err = airscope_config_store_save(proposed);
    }
    if (err == ESP_OK) {
        s_applied = *proposed;
        operation_succeed(&result, "Configuration applied and persisted");
        airscope_events_record(AIRSCOPE_EVENT_INFO, "config.applied",
                               "{\"result\":\"success\"}");
        goto done;
    }

    result.rollback_attempted = true;
    esp_err_t rollback = apply_runtime(&previous, restart);
    if (rollback == ESP_OK) {
        rollback = verify_runtime(&previous);
    }
    result.rollback_succeeded = rollback == ESP_OK;
    if (!result.rollback_succeeded) {
        s_degraded = true;
        airscope_events_record(AIRSCOPE_EVENT_ERROR, "config.rollback_failed",
                               "{\"recoveryRequired\":true}");
    } else {
        airscope_events_record(AIRSCOPE_EVENT_WARNING, "config.failed",
                               "{\"rollback\":\"success\"}");
    }
    operation_fail(&result, err, result.rollback_succeeded
                                     ? "Configuration failed; previous configuration restored"
                                     : "Configuration and rollback failed; physical recovery required");

done:
    s_latest_operation = result;
    xSemaphoreGive(s_mutex);
    return result;
}

airscope_operation_result_t airscope_wifi_switch_channel(
    const airscope_channel_request_t *request)
{
    airscope_operation_result_t result =
        operation_start(AIRSCOPE_OPERATION_CHANNEL_SWITCH);
    if (request == NULL) {
        operation_fail(&result, ESP_ERR_INVALID_ARG, "Channel request is required");
        return result;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    airscope_ap_config_t previous = s_applied;
    airscope_ap_config_t proposed = previous;
    proposed.primary_channel = request->primary_channel;
    proposed.bandwidth = request->bandwidth;
    proposed.secondary_channel = request->secondary_channel;
    proposed.csa_count = request->csa_count;
    airscope_validation_result_t validation = airscope_config_validate(&proposed, true);
    if (!validation.valid) {
        operation_fail(&result, ESP_ERR_INVALID_ARG, validation.message);
        goto done;
    }

    wifi_config_t wifi_config = make_wifi_config(&proposed);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err == ESP_OK) {
        err = esp_wifi_set_bandwidth(
            WIFI_IF_AP, proposed.bandwidth == AIRSCOPE_BANDWIDTH_HT40 ? WIFI_BW40
                                                                      : WIFI_BW20);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_channel(proposed.primary_channel,
                                   map_secondary(proposed.secondary_channel));
    }
    if (err == ESP_OK) {
        uint32_t timeout_ms =
            (uint32_t)proposed.csa_count * proposed.beacon_interval_tu * 1024 / 1000 + 750;
        uint32_t waited = 0;
        while (waited <= timeout_ms) {
            uint8_t primary;
            wifi_second_chan_t secondary;
            err = esp_wifi_get_channel(&primary, &secondary);
            if (err == ESP_OK && primary == proposed.primary_channel &&
                secondary == map_secondary(proposed.secondary_channel)) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            waited += 50;
        }
        if (waited > timeout_ms) {
            err = ESP_ERR_TIMEOUT;
        }
    }
    if (err == ESP_OK) {
        err = airscope_config_store_save(&proposed);
    }
    if (err == ESP_OK) {
        s_applied = proposed;
        operation_succeed(&result, "Channel switched through CSA and persisted");
        airscope_events_record(AIRSCOPE_EVENT_INFO, "channel.switched",
                               "{\"result\":\"success\"}");
        goto done;
    }

    result.rollback_attempted = true;
    wifi_config = make_wifi_config(&previous);
    esp_err_t rollback = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (rollback == ESP_OK) {
        rollback = esp_wifi_set_bandwidth(
            WIFI_IF_AP, previous.bandwidth == AIRSCOPE_BANDWIDTH_HT40 ? WIFI_BW40
                                                                      : WIFI_BW20);
    }
    if (rollback == ESP_OK) {
        rollback = esp_wifi_set_channel(previous.primary_channel,
                                        map_secondary(previous.secondary_channel));
    }
    result.rollback_succeeded = rollback == ESP_OK;
    if (!result.rollback_succeeded) {
        s_degraded = true;
        airscope_events_record(AIRSCOPE_EVENT_ERROR, "channel.rollback_failed",
                               "{\"recoveryRequired\":true}");
    } else {
        airscope_events_record(AIRSCOPE_EVENT_WARNING, "channel.failed",
                               "{\"rollback\":\"success\"}");
    }
    operation_fail(&result, err, result.rollback_succeeded
                                     ? "Channel switch failed; previous channel restored"
                                     : "Channel switch and rollback failed; physical recovery required");

done:
    s_latest_operation = result;
    xSemaphoreGive(s_mutex);
    return result;
}

airscope_operation_result_t airscope_wifi_latest_operation(void)
{
    if (s_mutex == NULL) {
        return (airscope_operation_result_t){0};
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    airscope_operation_result_t result = s_latest_operation;
    xSemaphoreGive(s_mutex);
    return result;
}

size_t airscope_wifi_get_clients(airscope_client_t *out, size_t capacity)
{
    if (out == NULL || capacity == 0 || !s_started) {
        return 0;
    }
    wifi_sta_list_t stations = {0};
    if (esp_wifi_ap_get_sta_list(&stations) != ESP_OK) {
        return 0;
    }
    size_t count = stations.num < capacity ? stations.num : capacity;
    for (size_t i = 0; i < count; ++i) {
        memcpy(out[i].mac, stations.sta[i].mac, sizeof(out[i].mac));
        out[i].rssi = stations.sta[i].rssi;
    }
    return count;
}

uint8_t airscope_wifi_client_count(void)
{
    wifi_sta_list_t stations = {0};
    return s_started && esp_wifi_ap_get_sta_list(&stations) == ESP_OK ? stations.num : 0;
}
