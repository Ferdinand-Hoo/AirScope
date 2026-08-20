#include "airscope_api.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airscope_auth.h"
#include "airscope_config.h"
#include "airscope_events.h"
#include "airscope_identity.h"
#include "airscope_wifi.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAX_JSON_BODY 2048
#define SESSION_COOKIE "airscope_session"
#define CONFIG_APPLY_DELAY_MS 1500
#define CONFIG_APPLY_TASK_STACK 4096
#define CONFIG_APPLY_TASK_PRIORITY 5

static const char *TAG = "airscope_api";
static httpd_handle_t s_server;
static airscope_https_identity_t s_identity;
static portMUX_TYPE s_config_update_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_config_update_pending;

typedef struct {
    airscope_ap_config_t config;
} deferred_config_update_t;

extern const uint8_t web_index_html_gz_start[]
    asm("_binary_index_html_gz_start");
extern const uint8_t web_index_html_gz_end[]
    asm("_binary_index_html_gz_end");
extern const uint8_t web_app_js_gz_start[]
    asm("_binary_app_js_gz_start");
extern const uint8_t web_app_js_gz_end[]
    asm("_binary_app_js_gz_end");
extern const uint8_t web_app_css_gz_start[]
    asm("_binary_app_css_gz_start");
extern const uint8_t web_app_css_gz_end[]
    asm("_binary_app_css_gz_end");

static esp_err_t send_json_text(httpd_req_t *request, const char *status,
                                const char *json)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_json(httpd_req_t *request, const char *status, cJSON *body)
{
    char *json = cJSON_PrintUnformatted(body);
    if (json == NULL) {
        cJSON_Delete(body);
        return send_json_text(request, "500 Internal Server Error",
                              "{\"error\":{\"code\":\"out_of_memory\","
                              "\"message\":\"Unable to serialize response\"}}");
    }
    esp_err_t err = send_json_text(request, status, json);
    cJSON_free(json);
    cJSON_Delete(body);
    return err;
}

static esp_err_t send_error(httpd_req_t *request, const char *status,
                            const char *code, const char *message,
                            const char *field)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *error = cJSON_AddObjectToObject(root, "error");
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    if (field != NULL) {
        cJSON_AddStringToObject(error, "field", field);
    }
    return send_json(request, status, root);
}

static cJSON *receive_json(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > MAX_JSON_BODY) {
        return NULL;
    }
    char *body = calloc(1, request->content_len + 1);
    if (body == NULL) {
        return NULL;
    }
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received,
                                   request->content_len - received);
        if (count <= 0) {
            free(body);
            return NULL;
        }
        received += (size_t)count;
    }
    cJSON *json = cJSON_ParseWithLength(body, received);
    free(body);
    return json;
}

static bool read_header(httpd_req_t *request, const char *name, char *output,
                        size_t output_size)
{
    size_t length = httpd_req_get_hdr_value_len(request, name);
    return length > 0 && length < output_size &&
           httpd_req_get_hdr_value_str(request, name, output, output_size) == ESP_OK;
}

static bool read_session_cookie(httpd_req_t *request,
                                char session[AIRSCOPE_SESSION_ID_LEN + 1])
{
    char cookies[512];
    if (!read_header(request, "Cookie", cookies, sizeof(cookies))) {
        return false;
    }
    const char *cursor = cookies;
    const size_t name_length = strlen(SESSION_COOKIE);
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == ';') {
            ++cursor;
        }
        if (strncmp(cursor, SESSION_COOKIE, name_length) == 0 &&
            cursor[name_length] == '=') {
            cursor += name_length + 1;
            const char *end = strchr(cursor, ';');
            size_t length = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
            if (length == AIRSCOPE_SESSION_ID_LEN) {
                memcpy(session, cursor, length);
                session[length] = '\0';
                return true;
            }
        }
        const char *next = strchr(cursor, ';');
        if (next == NULL) {
            break;
        }
        cursor = next + 1;
    }
    return false;
}

static bool authenticate(httpd_req_t *request, bool mutation)
{
    char authorization[AIRSCOPE_AUTOMATION_TOKEN_LEN + 16];
    if (read_header(request, "Authorization", authorization,
                    sizeof(authorization)) &&
        strncmp(authorization, "Bearer ", 7) == 0 &&
        airscope_auth_validate_bearer(authorization + 7)) {
        return true;
    }

    char session[AIRSCOPE_SESSION_ID_LEN + 1];
    char csrf[AIRSCOPE_CSRF_TOKEN_LEN + 1];
    const char *csrf_value = NULL;
    if (mutation &&
        read_header(request, "X-CSRF-Token", csrf, sizeof(csrf))) {
        csrf_value = csrf;
    }
    return read_session_cookie(request, session) &&
           airscope_auth_validate_session(session, csrf_value, mutation);
}

static esp_err_t require_authentication(httpd_req_t *request, bool mutation)
{
    if (authenticate(request, mutation)) {
        return ESP_OK;
    }
    send_error(request, "401 Unauthorized", "authentication_required",
               mutation ? "Valid session and CSRF token or Bearer token required"
                        : "Valid session or Bearer token required",
               NULL);
    return ESP_ERR_INVALID_STATE;
}

static cJSON *operation_json(const airscope_operation_result_t *operation)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "id", operation->id);
    cJSON_AddStringToObject(
        json, "type",
        operation->type == AIRSCOPE_OPERATION_CONFIG_TRANSACTION
            ? "configuration"
        : operation->type == AIRSCOPE_OPERATION_CHANNEL_SWITCH ? "channel-switch"
                                                               : "none");
    cJSON_AddBoolToObject(json, "success", operation->success);
    cJSON_AddBoolToObject(json, "rollbackAttempted",
                          operation->rollback_attempted);
    cJSON_AddBoolToObject(json, "rollbackSucceeded",
                          operation->rollback_succeeded);
    cJSON_AddNumberToObject(json, "error", operation->error);
    cJSON_AddStringToObject(json, "message", operation->message);
    return json;
}

static cJSON *config_json(const airscope_ap_config_t *config)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "schemaVersion", config->schema_version);
    cJSON_AddStringToObject(json, "ssid", config->ssid);
    cJSON_AddBoolToObject(json, "ssidHidden", config->ssid_hidden);
    cJSON_AddBoolToObject(json, "apPasswordConfigured",
                          config->ap_password[0] != '\0');
    cJSON_AddStringToObject(json, "authMode",
                            airscope_auth_mode_name(config->auth_mode));
    cJSON_AddStringToObject(json, "pairwiseCipher",
                            airscope_cipher_name(config->pairwise_cipher));
    cJSON_AddStringToObject(json, "pmf", airscope_pmf_name(config->pmf));
    cJSON_AddStringToObject(json, "saePwe",
                            airscope_sae_pwe_name(config->sae_pwe));
    cJSON_AddNumberToObject(json, "primaryChannel", config->primary_channel);
    cJSON_AddStringToObject(json, "bandwidth",
                            airscope_bandwidth_name(config->bandwidth));
    cJSON_AddStringToObject(
        json, "secondaryChannel",
        airscope_secondary_channel_name(config->secondary_channel));
    cJSON_AddNumberToObject(json, "csaCount", config->csa_count);
    cJSON_AddStringToObject(json, "protocol",
                            airscope_protocol_name(config->protocol));
    cJSON_AddNumberToObject(json, "maxTxPowerQuarterDbm",
                            config->max_tx_power_quarter_dbm);
    cJSON_AddNumberToObject(json, "maxClients", config->max_clients);
    cJSON_AddNumberToObject(json, "beaconIntervalTu",
                            config->beacon_interval_tu);
    cJSON_AddNumberToObject(json, "dtimPeriod", config->dtim_period);
    return json;
}

static bool json_string(cJSON *root, const char *name, const char **value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    *value = item->valuestring;
    return true;
}

static bool json_bool(cJSON *root, const char *name, bool *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsBool(item)) {
        return false;
    }
    *value = cJSON_IsTrue(item);
    return true;
}

static bool json_int(cJSON *root, const char *name, int *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item) || item->valuedouble != item->valueint) {
        return false;
    }
    *value = item->valueint;
    return true;
}

static bool parse_config(cJSON *json, airscope_ap_config_t *config,
                         const char **field)
{
    const char *value;
    int number;
    bool boolean;
    if (!cJSON_IsObject(json)) {
        *field = "config";
        return false;
    }
    if (!json_int(json, "schemaVersion", &number) ||
        number != AIRSCOPE_CONFIG_SCHEMA_VERSION) {
        *field = "schemaVersion";
        return false;
    }
    config->schema_version = (uint32_t)number;
    if (!json_string(json, "ssid", &value) ||
        strlen(value) > AIRSCOPE_SSID_MAX_LEN) {
        *field = "ssid";
        return false;
    }
    snprintf(config->ssid, sizeof(config->ssid), "%s", value);
    config->ssid_len = strlen(config->ssid);
    if (!json_bool(json, "ssidHidden", &boolean)) {
        *field = "ssidHidden";
        return false;
    }
    config->ssid_hidden = boolean;

    cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "apPassword");
    if (password != NULL) {
        if (!cJSON_IsString(password) ||
            strlen(password->valuestring) > AIRSCOPE_AP_PASSWORD_MAX_LEN) {
            *field = "apPassword";
            return false;
        }
        snprintf(config->ap_password, sizeof(config->ap_password), "%s",
                 password->valuestring);
    }
    if (!json_string(json, "authMode", &value) ||
        !airscope_config_parse_auth_mode(value, &config->auth_mode)) {
        *field = "authMode";
        return false;
    }
    if (!json_string(json, "pairwiseCipher", &value) ||
        !airscope_config_parse_cipher(value, &config->pairwise_cipher)) {
        *field = "pairwiseCipher";
        return false;
    }
    if (!json_string(json, "pmf", &value) ||
        !airscope_config_parse_pmf(value, &config->pmf)) {
        *field = "pmf";
        return false;
    }
    if (!json_string(json, "saePwe", &value) ||
        !airscope_config_parse_sae_pwe(value, &config->sae_pwe)) {
        *field = "saePwe";
        return false;
    }
    if (!json_int(json, "primaryChannel", &number)) {
        *field = "primaryChannel";
        return false;
    }
    config->primary_channel = number;
    if (!json_string(json, "bandwidth", &value) ||
        !airscope_config_parse_bandwidth(value, &config->bandwidth)) {
        *field = "bandwidth";
        return false;
    }
    if (!json_string(json, "secondaryChannel", &value) ||
        !airscope_config_parse_secondary_channel(
            value, &config->secondary_channel)) {
        *field = "secondaryChannel";
        return false;
    }
    if (!json_int(json, "csaCount", &number)) {
        *field = "csaCount";
        return false;
    }
    config->csa_count = number;
    if (!json_string(json, "protocol", &value) ||
        !airscope_config_parse_protocol(value, &config->protocol)) {
        *field = "protocol";
        return false;
    }
    if (!json_int(json, "maxTxPowerQuarterDbm", &number)) {
        *field = "maxTxPowerQuarterDbm";
        return false;
    }
    config->max_tx_power_quarter_dbm = number;
    if (!json_int(json, "maxClients", &number)) {
        *field = "maxClients";
        return false;
    }
    config->max_clients = number;
    if (!json_int(json, "beaconIntervalTu", &number)) {
        *field = "beaconIntervalTu";
        return false;
    }
    config->beacon_interval_tu = number;
    if (!json_int(json, "dtimPeriod", &number)) {
        *field = "dtimPeriod";
        return false;
    }
    config->dtim_period = number;
    return true;
}

static bool config_requires_reconnect(const airscope_ap_config_t *current,
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

static void deferred_config_apply_task(void *argument)
{
    deferred_config_update_t *update = argument;
    vTaskDelay(pdMS_TO_TICKS(CONFIG_APPLY_DELAY_MS));
    airscope_operation_result_t operation =
        airscope_wifi_apply_config(&update->config);
    if (!operation.success) {
        ESP_LOGE(TAG, "Deferred AP configuration failed: %s (0x%x)",
                 operation.message, (unsigned int)operation.error);
    }
    explicit_bzero(update, sizeof(*update));
    free(update);

    portENTER_CRITICAL(&s_config_update_lock);
    s_config_update_pending = false;
    portEXIT_CRITICAL(&s_config_update_lock);
    vTaskDelete(NULL);
}

static esp_err_t session_create_handler(httpd_req_t *request)
{
    cJSON *json = receive_json(request);
    const char *password = NULL;
    if (json == NULL || !json_string(json, "password", &password)) {
        cJSON_Delete(json);
        return send_error(request, "400 Bad Request", "invalid_request",
                          "Password is required", "password");
    }
    bool valid = airscope_auth_verify_password(password);
    cJSON_Delete(json);
    if (!valid) {
        airscope_events_record(AIRSCOPE_EVENT_WARNING, "auth.login_failed", "{}");
        return send_error(request, "401 Unauthorized", "invalid_credential",
                          "Management credential is invalid", NULL);
    }

    char session[AIRSCOPE_SESSION_ID_LEN + 1];
    char csrf[AIRSCOPE_CSRF_TOKEN_LEN + 1];
    esp_err_t err = airscope_auth_create_session(session, csrf);
    if (err != ESP_OK) {
        return send_error(request, "503 Service Unavailable",
                          "session_capacity", "No session slot is available",
                          NULL);
    }
    char cookie[160];
    snprintf(cookie, sizeof(cookie),
             SESSION_COOKIE "=%s; Path=/; Max-Age=1800; Secure; HttpOnly; "
                            "SameSite=Strict",
             session);
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "csrfToken", csrf);
    cJSON_AddNumberToObject(response, "expiresInSeconds", 1800);
    airscope_events_record(AIRSCOPE_EVENT_INFO, "auth.login_succeeded", "{}");
    return send_json(request, "201 Created", response);
}

static esp_err_t session_delete_handler(httpd_req_t *request)
{
    if (require_authentication(request, true) != ESP_OK) {
        return ESP_OK;
    }
    char session[AIRSCOPE_SESSION_ID_LEN + 1];
    if (read_session_cookie(request, session)) {
        airscope_auth_delete_session(session);
    }
    httpd_resp_set_hdr(
        request, "Set-Cookie",
        SESSION_COOKIE "=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Strict");
    return httpd_resp_send(request, NULL, 0);
}

static void add_string_array(cJSON *root, const char *name,
                             const char *const *values, size_t count)
{
    cJSON *array = cJSON_AddArrayToObject(root, name);
    for (size_t i = 0; i < count; ++i) {
        cJSON_AddItemToArray(array, cJSON_CreateString(values[i]));
    }
}

static esp_err_t capabilities_handler(httpd_req_t *request)
{
    if (require_authentication(request, false) != ESP_OK) {
        return ESP_OK;
    }
    static const char *const auth_modes[] = {
        "open", "wpa-psk", "wpa2-psk", "wpa-wpa2-psk", "wpa3-psk",
        "wpa2-wpa3-psk",
    };
    static const char *const ciphers[] = {
        "none", "tkip", "ccmp", "tkip-ccmp", "gcmp", "gcmp256",
    };
    static const char *const pmf[] = {"disabled", "optional", "required"};
    static const char *const sae[] = {"hunt-and-peck", "hash-to-element", "both"};
    static const char *const protocols[] = {"b", "bg", "bgn", "gn"};
    static const char *const bandwidths[] = {"ht20", "ht40"};
    static const char *const secondary[] = {"none", "above", "below"};
    static const int powers[] = {8, 20, 28, 34, 44, 52, 56, 60, 66, 72, 80};

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schemaVersion", AIRSCOPE_CONFIG_SCHEMA_VERSION);
    add_string_array(root, "authModes", auth_modes,
                     sizeof(auth_modes) / sizeof(auth_modes[0]));
    add_string_array(root, "pairwiseCiphers", ciphers,
                     sizeof(ciphers) / sizeof(ciphers[0]));
    add_string_array(root, "pmfModes", pmf, sizeof(pmf) / sizeof(pmf[0]));
    add_string_array(root, "saePweMethods", sae, sizeof(sae) / sizeof(sae[0]));
    add_string_array(root, "protocols", protocols,
                     sizeof(protocols) / sizeof(protocols[0]));
    add_string_array(root, "bandwidths", bandwidths,
                     sizeof(bandwidths) / sizeof(bandwidths[0]));
    add_string_array(root, "secondaryChannels", secondary,
                     sizeof(secondary) / sizeof(secondary[0]));
    cJSON *channels = cJSON_AddArrayToObject(root, "primaryChannels");
    for (int i = 1; i <= 13; ++i) {
        cJSON_AddItemToArray(channels, cJSON_CreateNumber(i));
    }
    cJSON *tx_power = cJSON_AddArrayToObject(root, "txPowerQuarterDbm");
    for (size_t i = 0; i < sizeof(powers) / sizeof(powers[0]); ++i) {
        cJSON_AddItemToArray(tx_power, cJSON_CreateNumber(powers[i]));
    }
    cJSON *limits = cJSON_AddObjectToObject(root, "limits");
    cJSON_AddNumberToObject(limits, "maxClients", 10);
    cJSON_AddNumberToObject(limits, "maxTokens",
                            AIRSCOPE_MAX_AUTOMATION_TOKENS);
    cJSON_AddNumberToObject(limits, "eventCapacity", AIRSCOPE_EVENT_CAPACITY);
    return send_json(request, "200 OK", root);
}

static esp_err_t status_handler(httpd_req_t *request)
{
    if (require_authentication(request, false) != ESP_OK) {
        return ESP_OK;
    }
    airscope_ap_config_t config;
    airscope_wifi_get_applied_config(&config);
    airscope_operation_result_t operation = airscope_wifi_latest_operation();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "started", airscope_wifi_is_started());
    cJSON_AddBoolToObject(root, "degraded", airscope_wifi_is_degraded());
    cJSON_AddStringToObject(root, "managementAddress", "https://192.168.4.1");
    cJSON_AddNumberToObject(root, "uptimeMs", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(root, "bootId", airscope_events_boot_id());
    cJSON_AddNumberToObject(root, "clientCount", airscope_wifi_client_count());
    cJSON_AddItemToObject(root, "appliedConfig", config_json(&config));
    if (operation.type != AIRSCOPE_OPERATION_NONE) {
        cJSON_AddItemToObject(root, "latestOperation", operation_json(&operation));
    } else {
        cJSON_AddNullToObject(root, "latestOperation");
    }
    airscope_token_summary_t tokens[AIRSCOPE_MAX_AUTOMATION_TOKENS];
    cJSON_AddNumberToObject(
        root, "automationTokenCount",
        airscope_auth_list_tokens(tokens, AIRSCOPE_MAX_AUTOMATION_TOKENS));
    return send_json(request, "200 OK", root);
}

static esp_err_t config_get_handler(httpd_req_t *request)
{
    if (require_authentication(request, false) != ESP_OK) {
        return ESP_OK;
    }
    airscope_ap_config_t config;
    airscope_wifi_get_applied_config(&config);
    return send_json(request, "200 OK", config_json(&config));
}

static esp_err_t config_put_handler(httpd_req_t *request)
{
    if (require_authentication(request, true) != ESP_OK) {
        return ESP_OK;
    }
    cJSON *json = receive_json(request);
    airscope_ap_config_t config;
    airscope_ap_config_t current;
    airscope_wifi_get_applied_config(&current);
    config = current;
    const char *field = NULL;
    if (json == NULL || !parse_config(json, &config, &field)) {
        cJSON_Delete(json);
        return send_error(request, "400 Bad Request", "invalid_request",
                          "Complete valid configuration is required", field);
    }
    cJSON_Delete(json);

    airscope_validation_result_t validation =
        airscope_config_validate(&config, true);
    if (!validation.valid) {
        return send_error(request, "400 Bad Request", "invalid_configuration",
                          validation.message, validation.field);
    }
    if (config.primary_channel != current.primary_channel ||
        config.bandwidth != current.bandwidth ||
        config.secondary_channel != current.secondary_channel ||
        config.csa_count != current.csa_count) {
        return send_error(request, "400 Bad Request", "channel_change_required",
                          "Channel fields require a Channel Switch Operation",
                          "primaryChannel");
    }

    deferred_config_update_t *update = calloc(1, sizeof(*update));
    if (update == NULL) {
        return send_error(request, "503 Service Unavailable", "out_of_memory",
                          "Unable to schedule configuration update", NULL);
    }
    update->config = config;

    portENTER_CRITICAL(&s_config_update_lock);
    bool already_pending = s_config_update_pending;
    if (!already_pending) {
        s_config_update_pending = true;
    }
    portEXIT_CRITICAL(&s_config_update_lock);
    if (already_pending) {
        explicit_bzero(update, sizeof(*update));
        free(update);
        return send_error(request, "409 Conflict", "configuration_pending",
                          "Another configuration update is still pending", NULL);
    }

    if (xTaskCreate(deferred_config_apply_task, "ap_config_apply",
                    CONFIG_APPLY_TASK_STACK, update,
                    CONFIG_APPLY_TASK_PRIORITY, NULL) != pdPASS) {
        portENTER_CRITICAL(&s_config_update_lock);
        s_config_update_pending = false;
        portEXIT_CRITICAL(&s_config_update_lock);
        explicit_bzero(update, sizeof(*update));
        free(update);
        return send_error(request, "503 Service Unavailable", "task_unavailable",
                          "Unable to schedule configuration update", NULL);
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "accepted", true);
    cJSON_AddBoolToObject(response, "reconnectRequired",
                          config_requires_reconnect(&current, &config));
    cJSON_AddNumberToObject(response, "applyDelayMs", CONFIG_APPLY_DELAY_MS);
    return send_json(request, "202 Accepted", response);
}

static esp_err_t channel_switch_handler(httpd_req_t *request)
{
    if (require_authentication(request, true) != ESP_OK) {
        return ESP_OK;
    }
    cJSON *json = receive_json(request);
    airscope_channel_request_t channel = {0};
    const char *value;
    int primary_channel = 0;
    int csa_count = 0;
    bool valid = json != NULL && cJSON_IsObject(json) &&
                 json_int(json, "primaryChannel", &primary_channel) &&
                 json_string(json, "bandwidth", &value) &&
                 airscope_config_parse_bandwidth(value, &channel.bandwidth) &&
                 json_string(json, "secondaryChannel", &value) &&
                 airscope_config_parse_secondary_channel(
                     value, &channel.secondary_channel) &&
                 json_int(json, "csaCount", &csa_count);
    channel.primary_channel = primary_channel;
    channel.csa_count = csa_count;
    cJSON_Delete(json);
    if (!valid) {
        return send_error(request, "400 Bad Request", "invalid_request",
                          "Channel, bandwidth, secondary direction, and CSA count are required",
                          NULL);
    }
    airscope_operation_result_t operation =
        airscope_wifi_switch_channel(&channel);
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "operationId", operation.id);
    cJSON_AddItemToObject(response, "result", operation_json(&operation));
    return send_json(request, operation.success ? "200 OK" : "409 Conflict",
                     response);
}

static esp_err_t clients_handler(httpd_req_t *request)
{
    if (require_authentication(request, false) != ESP_OK) {
        return ESP_OK;
    }
    airscope_client_t clients[AIRSCOPE_MAX_CLIENTS];
    size_t count = airscope_wifi_get_clients(clients, AIRSCOPE_MAX_CLIENTS);
    cJSON *root = cJSON_CreateObject();
    cJSON *array = cJSON_AddArrayToObject(root, "clients");
    for (size_t i = 0; i < count; ++i) {
        cJSON *client = cJSON_CreateObject();
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 clients[i].mac[0], clients[i].mac[1], clients[i].mac[2],
                 clients[i].mac[3], clients[i].mac[4], clients[i].mac[5]);
        cJSON_AddStringToObject(client, "mac", mac);
        cJSON_AddNumberToObject(client, "rssi", clients[i].rssi);
        cJSON_AddItemToArray(array, client);
    }
    cJSON_AddNumberToObject(root, "count", count);
    return send_json(request, "200 OK", root);
}

static esp_err_t events_handler(httpd_req_t *request)
{
    if (require_authentication(request, false) != ESP_OK) {
        return ESP_OK;
    }
    airscope_runtime_event_t *events =
        calloc(AIRSCOPE_EVENT_CAPACITY, sizeof(*events));
    if (events == NULL) {
        return send_error(request, "500 Internal Server Error",
                          "out_of_memory", "Unable to snapshot events", NULL);
    }
    size_t count =
        airscope_events_snapshot(events, AIRSCOPE_EVENT_CAPACITY);
    cJSON *root = cJSON_CreateObject();
    cJSON *array = cJSON_AddArrayToObject(root, "events");
    for (size_t i = 0; i < count; ++i) {
        cJSON *event = cJSON_CreateObject();
        cJSON_AddNumberToObject(event, "bootId", events[i].boot_id);
        cJSON_AddNumberToObject(event, "sequence", events[i].sequence);
        cJSON_AddNumberToObject(event, "uptimeMs", events[i].uptime_ms);
        cJSON_AddStringToObject(
            event, "severity",
            airscope_event_severity_name(events[i].severity));
        cJSON_AddStringToObject(event, "type", events[i].type);
        cJSON *details = cJSON_Parse(events[i].details);
        cJSON_AddItemToObject(event, "details",
                              details != NULL ? details : cJSON_CreateObject());
        cJSON_AddItemToArray(array, event);
    }
    free(events);
    cJSON_AddNumberToObject(root, "count", count);
    return send_json(request, "200 OK", root);
}

static void add_token_summaries(cJSON *root)
{
    airscope_token_summary_t tokens[AIRSCOPE_MAX_AUTOMATION_TOKENS];
    size_t count =
        airscope_auth_list_tokens(tokens, AIRSCOPE_MAX_AUTOMATION_TOKENS);
    cJSON *array = cJSON_AddArrayToObject(root, "tokens");
    for (size_t i = 0; i < count; ++i) {
        cJSON *token = cJSON_CreateObject();
        cJSON_AddStringToObject(token, "id", tokens[i].id);
        cJSON_AddStringToObject(token, "label", tokens[i].label);
        cJSON_AddItemToArray(array, token);
    }
}

static esp_err_t tokens_get_handler(httpd_req_t *request)
{
    if (require_authentication(request, false) != ESP_OK) {
        return ESP_OK;
    }
    cJSON *root = cJSON_CreateObject();
    add_token_summaries(root);
    return send_json(request, "200 OK", root);
}

static esp_err_t token_create_handler(httpd_req_t *request)
{
    if (require_authentication(request, true) != ESP_OK) {
        return ESP_OK;
    }
    cJSON *json = receive_json(request);
    const char *label = NULL;
    if (json == NULL || !json_string(json, "label", &label) ||
        label[0] == '\0' || strlen(label) > 32) {
        cJSON_Delete(json);
        return send_error(request, "400 Bad Request", "invalid_request",
                          "Token label must contain 1 to 32 characters",
                          "label");
    }
    char label_copy[33];
    snprintf(label_copy, sizeof(label_copy), "%s", label);
    cJSON_Delete(json);
    char token_id[AIRSCOPE_TOKEN_ID_LEN + 1];
    char plaintext[AIRSCOPE_AUTOMATION_TOKEN_LEN + 1];
    esp_err_t err = airscope_auth_create_automation_token(
        label_copy, token_id, plaintext);
    if (err != ESP_OK) {
        return send_error(request, "409 Conflict", "token_capacity",
                          "Unable to create another Automation Token", NULL);
    }
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "id", token_id);
    cJSON_AddStringToObject(response, "label", label_copy);
    cJSON_AddStringToObject(response, "token", plaintext);
    cJSON_AddStringToObject(response, "display", "one-time");
    airscope_events_record(AIRSCOPE_EVENT_INFO, "auth.token_created", "{}");
    return send_json(request, "201 Created", response);
}

static esp_err_t token_delete_handler(httpd_req_t *request)
{
    if (require_authentication(request, true) != ESP_OK) {
        return ESP_OK;
    }
    static const char prefix[] = "/api/v1/tokens/";
    const char *token_id = request->uri + sizeof(prefix) - 1;
    if (strlen(token_id) != AIRSCOPE_TOKEN_ID_LEN) {
        return send_error(request, "400 Bad Request", "invalid_request",
                          "Token identifier is invalid", "id");
    }
    esp_err_t err = airscope_auth_revoke_automation_token(token_id);
    if (err == ESP_ERR_NOT_FOUND) {
        return send_error(request, "404 Not Found", "not_found",
                          "Automation Token does not exist", NULL);
    }
    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error",
                          "persistence_failed",
                          "Automation Token could not be revoked", NULL);
    }
    airscope_events_record(AIRSCOPE_EVENT_INFO, "auth.token_revoked", "{}");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t credential_handler(httpd_req_t *request)
{
    if (require_authentication(request, true) != ESP_OK) {
        return ESP_OK;
    }
    cJSON *json = receive_json(request);
    const char *password = NULL;
    if (json == NULL || !json_string(json, "password", &password) ||
        strlen(password) < 12 || strlen(password) > AIRSCOPE_PASSWORD_MAX_LEN) {
        cJSON_Delete(json);
        return send_error(request, "400 Bad Request", "invalid_request",
                          "Management credential must contain 12 to 63 characters",
                          "password");
    }
    char password_copy[AIRSCOPE_PASSWORD_MAX_LEN + 1];
    snprintf(password_copy, sizeof(password_copy), "%s", password);
    cJSON_Delete(json);
    esp_err_t err = airscope_auth_rotate_password(password_copy);
    if (err != ESP_OK) {
        return send_error(request, "500 Internal Server Error",
                          "persistence_failed",
                          "Management credential could not be rotated", NULL);
    }
    airscope_events_record(AIRSCOPE_EVENT_INFO, "auth.credential_rotated", "{}");
    httpd_resp_set_hdr(
        request, "Set-Cookie",
        SESSION_COOKIE "=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Strict");
    return httpd_resp_send(request, NULL, 0);
}

static esp_err_t send_embedded_gzip(httpd_req_t *request, const char *type,
                                    const uint8_t *start, const uint8_t *end,
                                    bool immutable)
{
    httpd_resp_set_type(request, type);
    httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(request, "Vary", "Accept-Encoding");
    httpd_resp_set_hdr(
        request, "Cache-Control",
        immutable ? "public, max-age=31536000, immutable" : "no-store");
    return httpd_resp_send(request, (const char *)start, end - start);
}

static esp_err_t root_handler(httpd_req_t *request)
{
    return send_embedded_gzip(request, "text/html; charset=utf-8",
                              web_index_html_gz_start,
                              web_index_html_gz_end, false);
}

static esp_err_t app_js_handler(httpd_req_t *request)
{
    return send_embedded_gzip(request, "text/javascript; charset=utf-8",
                              web_app_js_gz_start, web_app_js_gz_end, true);
}

static esp_err_t app_css_handler(httpd_req_t *request)
{
    return send_embedded_gzip(request, "text/css; charset=utf-8",
                              web_app_css_gz_start, web_app_css_gz_end, true);
}

static esp_err_t register_handler(httpd_method_t method, const char *uri,
                                  esp_err_t (*handler)(httpd_req_t *))
{
    const httpd_uri_t definition = {
        .uri = uri,
        .method = method,
        .handler = handler,
    };
    return httpd_register_uri_handler(s_server, &definition);
}

esp_err_t airscope_api_start(void)
{
    if (s_server != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(airscope_identity_load_or_create(&s_identity), TAG,
                        "HTTPS identity unavailable");

    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd.max_uri_handlers = 20;
    config.httpd.stack_size = 12288;
    config.httpd.uri_match_fn = httpd_uri_match_wildcard;
    config.servercert = s_identity.certificate_pem;
    config.servercert_len = s_identity.certificate_len;
    config.prvtkey_pem = s_identity.private_key_pem;
    config.prvtkey_len = s_identity.private_key_len;
    esp_err_t err = httpd_ssl_start(&s_server, &config);
    if (err != ESP_OK) {
        airscope_identity_free(&s_identity);
        return err;
    }

    ESP_RETURN_ON_ERROR(register_handler(HTTP_POST, "/api/v1/session",
                                         session_create_handler),
                        TAG, "session POST registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_DELETE, "/api/v1/session",
                                         session_delete_handler),
                        TAG, "session DELETE registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_GET, "/api/v1/capabilities",
                                         capabilities_handler),
                        TAG, "capabilities registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_GET, "/api/v1/status",
                                         status_handler),
                        TAG, "status registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_GET, "/api/v1/config",
                                         config_get_handler),
                        TAG, "config GET registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_PUT, "/api/v1/config",
                                         config_put_handler),
                        TAG, "config PUT registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_POST, "/api/v1/channel-switch",
                                         channel_switch_handler),
                        TAG, "channel registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_GET, "/api/v1/clients",
                                         clients_handler),
                        TAG, "clients registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_GET, "/api/v1/events",
                                         events_handler),
                        TAG, "events registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_GET, "/api/v1/tokens",
                                         tokens_get_handler),
                        TAG, "tokens GET registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_POST, "/api/v1/tokens",
                                         token_create_handler),
                        TAG, "tokens POST registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_DELETE, "/api/v1/tokens/*",
                                         token_delete_handler),
                        TAG, "tokens DELETE registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_PUT, "/api/v1/credential",
                                         credential_handler),
                        TAG, "credential registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_GET, "/", root_handler), TAG,
                        "root registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_GET, "/assets/app.js",
                                         app_js_handler),
                        TAG, "application script registration failed");
    ESP_RETURN_ON_ERROR(register_handler(HTTP_GET, "/assets/app.css",
                                         app_css_handler),
                        TAG, "application stylesheet registration failed");

    airscope_events_record(AIRSCOPE_EVENT_INFO, "management.started",
                           "{\"protocol\":\"https\"}");
    ESP_LOGI(TAG, "HTTPS management API started");
    return ESP_OK;
}
