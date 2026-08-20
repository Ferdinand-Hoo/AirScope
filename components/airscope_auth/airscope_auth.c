#include "airscope_auth.h"

#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "psa/crypto.h"

#define AUTH_NAMESPACE "airscope_auth"
#define CREDENTIAL_KEY "credential"
#define TOKENS_KEY "tokens"
#define SALT_LEN 16
#define HASH_LEN 32
#define HASH_ROUNDS 12000
#define MAX_SESSIONS 8
#define SESSION_LIFETIME_US (30LL * 60LL * 1000000LL)
#define DEFAULT_MANAGEMENT_PASSWORD "admin"
#define MIN_ROTATED_PASSWORD_LEN 12

typedef struct {
    uint8_t salt[SALT_LEN];
    uint8_t hash[HASH_LEN];
} stored_credential_t;

typedef struct {
    bool active;
    char id[AIRSCOPE_TOKEN_ID_LEN + 1];
    char label[33];
    uint8_t salt[SALT_LEN];
    uint8_t hash[HASH_LEN];
} stored_token_t;

typedef struct {
    bool active;
    char id[AIRSCOPE_SESSION_ID_LEN + 1];
    char csrf[AIRSCOPE_CSRF_TOKEN_LEN + 1];
    int64_t expires_at_us;
} session_t;

static nvs_handle_t s_nvs;
static stored_credential_t s_credential;
static stored_token_t s_tokens[AIRSCOPE_MAX_AUTOMATION_TOKENS];
static session_t s_sessions[MAX_SESSIONS];
static SemaphoreHandle_t s_mutex;

static bool hash_secret(const char *secret, const uint8_t salt[SALT_LEN],
                        uint8_t output[HASH_LEN])
{
    uint8_t input[SALT_LEN + AIRSCOPE_PASSWORD_MAX_LEN];
    size_t secret_len = strlen(secret);
    if (secret_len > sizeof(input) - SALT_LEN) {
        return false;
    }
    memcpy(input, salt, SALT_LEN);
    memcpy(input + SALT_LEN, secret, secret_len);
    size_t output_len = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, input, SALT_LEN + secret_len, output,
                         HASH_LEN, &output_len) != PSA_SUCCESS ||
        output_len != HASH_LEN) {
        return false;
    }
    for (unsigned i = 1; i < HASH_ROUNDS; ++i) {
        memcpy(input, salt, SALT_LEN);
        memcpy(input + SALT_LEN, output, HASH_LEN);
        if (psa_hash_compute(PSA_ALG_SHA_256, input, SALT_LEN + HASH_LEN, output,
                             HASH_LEN, &output_len) != PSA_SUCCESS ||
            output_len != HASH_LEN) {
            return false;
        }
    }
    return true;
}

static bool constant_time_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    uint8_t difference = 0;
    for (size_t i = 0; i < size; ++i) {
        difference |= left[i] ^ right[i];
    }
    return difference == 0;
}

static void fill_random(uint8_t *out, size_t size)
{
    esp_fill_random(out, size);
}

esp_err_t airscope_auth_generate_secret(char *out, size_t out_size, size_t length)
{
    static const char alphabet[] =
        "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    if (out == NULL || length == 0 || out_size <= length) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < length; ++i) {
        uint8_t random;
        fill_random(&random, 1);
        out[i] = alphabet[random % (sizeof(alphabet) - 1)];
    }
    out[length] = '\0';
    return ESP_OK;
}

static esp_err_t save_credential(void)
{
    esp_err_t err = nvs_set_blob(s_nvs, CREDENTIAL_KEY, &s_credential, sizeof(s_credential));
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

static esp_err_t save_tokens(void)
{
    esp_err_t err = nvs_set_blob(s_nvs, TOKENS_KEY, s_tokens, sizeof(s_tokens));
    return err == ESP_OK ? nvs_commit(s_nvs) : err;
}

static esp_err_t set_password(const char *password, size_t minimum_length)
{
    if (password == NULL || strlen(password) < minimum_length ||
        strlen(password) > AIRSCOPE_PASSWORD_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    fill_random(s_credential.salt, sizeof(s_credential.salt));
    if (!hash_secret(password, s_credential.salt, s_credential.hash)) {
        return ESP_FAIL;
    }
    return save_credential();
}

esp_err_t airscope_auth_init(char *generated_password, size_t generated_password_size,
                             bool *password_generated)
{
    if (password_generated == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        return ESP_FAIL;
    }
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    esp_err_t err = nvs_open(AUTH_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t size = sizeof(s_credential);
    err = nvs_get_blob(s_nvs, CREDENTIAL_KEY, &s_credential, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND || size != sizeof(s_credential)) {
        if (generated_password == NULL ||
            generated_password_size < sizeof(DEFAULT_MANAGEMENT_PASSWORD)) {
            return ESP_ERR_INVALID_SIZE;
        }
        snprintf(generated_password, generated_password_size, "%s",
                 DEFAULT_MANAGEMENT_PASSWORD);
        err = set_password(generated_password, 1);
        if (err != ESP_OK) {
            return err;
        }
        *password_generated = true;
    } else if (err != ESP_OK) {
        return err;
    } else {
        *password_generated = false;
        if (generated_password != NULL && generated_password_size > 0) {
            generated_password[0] = '\0';
        }
    }

    memset(s_tokens, 0, sizeof(s_tokens));
    size = sizeof(s_tokens);
    err = nvs_get_blob(s_nvs, TOKENS_KEY, s_tokens, &size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    memset(s_sessions, 0, sizeof(s_sessions));
    return ESP_OK;
}

bool airscope_auth_verify_password(const char *password)
{
    if (password == NULL) {
        return false;
    }
    uint8_t hash[HASH_LEN];
    if (!hash_secret(password, s_credential.salt, hash)) {
        return false;
    }
    return constant_time_equal(hash, s_credential.hash, sizeof(hash));
}

esp_err_t airscope_auth_rotate_password(const char *new_password)
{
    esp_err_t err = set_password(new_password, MIN_ROTATED_PASSWORD_LEN);
    if (err == ESP_OK) {
        airscope_auth_revoke_all_sessions();
    }
    return err;
}

esp_err_t airscope_auth_reset(char *generated_password, size_t generated_password_size)
{
    if (generated_password == NULL ||
        generated_password_size < sizeof(DEFAULT_MANAGEMENT_PASSWORD)) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(generated_password, generated_password_size, "%s",
             DEFAULT_MANAGEMENT_PASSWORD);
    esp_err_t err = set_password(generated_password, 1);
    if (err == ESP_OK) {
        airscope_auth_revoke_all_sessions();
        err = airscope_auth_revoke_all_tokens();
    }
    return err;
}

esp_err_t airscope_auth_create_session(char session_id[AIRSCOPE_SESSION_ID_LEN + 1],
                                       char csrf_token[AIRSCOPE_CSRF_TOKEN_LEN + 1])
{
    if (session_id == NULL || csrf_token == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int slot = -1;
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < MAX_SESSIONS; ++i) {
        if (!s_sessions[i].active || s_sessions[i].expires_at_us <= now) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }
    airscope_auth_generate_secret(s_sessions[slot].id, sizeof(s_sessions[slot].id),
                                  AIRSCOPE_SESSION_ID_LEN);
    airscope_auth_generate_secret(s_sessions[slot].csrf, sizeof(s_sessions[slot].csrf),
                                  AIRSCOPE_CSRF_TOKEN_LEN);
    s_sessions[slot].active = true;
    s_sessions[slot].expires_at_us = now + SESSION_LIFETIME_US;
    snprintf(session_id, AIRSCOPE_SESSION_ID_LEN + 1, "%s", s_sessions[slot].id);
    snprintf(csrf_token, AIRSCOPE_CSRF_TOKEN_LEN + 1, "%s", s_sessions[slot].csrf);
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

bool airscope_auth_validate_session(const char *session_id, const char *csrf_token,
                                    bool mutation)
{
    if (session_id == NULL) {
        return false;
    }
    bool valid = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < MAX_SESSIONS; ++i) {
        if (s_sessions[i].active && s_sessions[i].expires_at_us <= now) {
            s_sessions[i].active = false;
        }
        if (s_sessions[i].active && strcmp(s_sessions[i].id, session_id) == 0 &&
            (!mutation || (csrf_token != NULL && strcmp(s_sessions[i].csrf, csrf_token) == 0))) {
            s_sessions[i].expires_at_us = now + SESSION_LIFETIME_US;
            valid = true;
            break;
        }
    }
    xSemaphoreGive(s_mutex);
    return valid;
}

void airscope_auth_delete_session(const char *session_id)
{
    if (session_id == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_SESSIONS; ++i) {
        if (s_sessions[i].active && strcmp(s_sessions[i].id, session_id) == 0) {
            s_sessions[i].active = false;
            break;
        }
    }
    xSemaphoreGive(s_mutex);
}

void airscope_auth_revoke_all_sessions(void)
{
    if (s_mutex == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(s_sessions, 0, sizeof(s_sessions));
    xSemaphoreGive(s_mutex);
}

esp_err_t airscope_auth_create_automation_token(
    const char *label, char token_id[AIRSCOPE_TOKEN_ID_LEN + 1],
    char plaintext[AIRSCOPE_AUTOMATION_TOKEN_LEN + 1])
{
    if (label == NULL || token_id == NULL || plaintext == NULL || strlen(label) > 32) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < AIRSCOPE_MAX_AUTOMATION_TOKENS; ++i) {
        if (!s_tokens[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }
    airscope_auth_generate_secret(s_tokens[slot].id, sizeof(s_tokens[slot].id),
                                  AIRSCOPE_TOKEN_ID_LEN);
    airscope_auth_generate_secret(plaintext, AIRSCOPE_AUTOMATION_TOKEN_LEN + 1,
                                  AIRSCOPE_AUTOMATION_TOKEN_LEN);
    snprintf(s_tokens[slot].label, sizeof(s_tokens[slot].label), "%s", label);
    fill_random(s_tokens[slot].salt, sizeof(s_tokens[slot].salt));
    if (!hash_secret(plaintext, s_tokens[slot].salt, s_tokens[slot].hash)) {
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }
    s_tokens[slot].active = true;
    snprintf(token_id, AIRSCOPE_TOKEN_ID_LEN + 1, "%s", s_tokens[slot].id);
    esp_err_t err = save_tokens();
    if (err != ESP_OK) {
        memset(&s_tokens[slot], 0, sizeof(s_tokens[slot]));
    }
    xSemaphoreGive(s_mutex);
    return err;
}

bool airscope_auth_validate_bearer(const char *plaintext)
{
    if (plaintext == NULL) {
        return false;
    }
    bool valid = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < AIRSCOPE_MAX_AUTOMATION_TOKENS; ++i) {
        if (!s_tokens[i].active) {
            continue;
        }
        uint8_t hash[HASH_LEN];
        if (!hash_secret(plaintext, s_tokens[i].salt, hash)) {
            continue;
        }
        if (constant_time_equal(hash, s_tokens[i].hash, sizeof(hash))) {
            valid = true;
            break;
        }
    }
    xSemaphoreGive(s_mutex);
    return valid;
}

esp_err_t airscope_auth_revoke_automation_token(const char *token_id)
{
    if (token_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (int i = 0; i < AIRSCOPE_MAX_AUTOMATION_TOKENS; ++i) {
        if (s_tokens[i].active && strcmp(s_tokens[i].id, token_id) == 0) {
            memset(&s_tokens[i], 0, sizeof(s_tokens[i]));
            err = save_tokens();
            break;
        }
    }
    xSemaphoreGive(s_mutex);
    return err;
}

size_t airscope_auth_list_tokens(airscope_token_summary_t *out, size_t capacity)
{
    if (out == NULL || capacity == 0) {
        return 0;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t count = 0;
    for (int i = 0; i < AIRSCOPE_MAX_AUTOMATION_TOKENS && count < capacity; ++i) {
        if (!s_tokens[i].active) {
            continue;
        }
        out[count].active = true;
        snprintf(out[count].id, sizeof(out[count].id), "%s", s_tokens[i].id);
        snprintf(out[count].label, sizeof(out[count].label), "%s", s_tokens[i].label);
        ++count;
    }
    xSemaphoreGive(s_mutex);
    return count;
}

esp_err_t airscope_auth_revoke_all_tokens(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(s_tokens, 0, sizeof(s_tokens));
    esp_err_t err = save_tokens();
    xSemaphoreGive(s_mutex);
    return err;
}
