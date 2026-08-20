#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIRSCOPE_PASSWORD_MAX_LEN 63
#define AIRSCOPE_SESSION_ID_LEN 32
#define AIRSCOPE_CSRF_TOKEN_LEN 32
#define AIRSCOPE_AUTOMATION_TOKEN_LEN 48
#define AIRSCOPE_TOKEN_ID_LEN 16
#define AIRSCOPE_MAX_AUTOMATION_TOKENS 8

typedef struct {
    char id[AIRSCOPE_TOKEN_ID_LEN + 1];
    char label[33];
    bool active;
} airscope_token_summary_t;

esp_err_t airscope_auth_init(char *generated_password, size_t generated_password_size,
                             bool *password_generated);
bool airscope_auth_verify_password(const char *password);
esp_err_t airscope_auth_rotate_password(const char *new_password);
esp_err_t airscope_auth_reset(char *generated_password, size_t generated_password_size);

esp_err_t airscope_auth_create_session(char session_id[AIRSCOPE_SESSION_ID_LEN + 1],
                                       char csrf_token[AIRSCOPE_CSRF_TOKEN_LEN + 1]);
bool airscope_auth_validate_session(const char *session_id, const char *csrf_token,
                                    bool mutation);
void airscope_auth_delete_session(const char *session_id);
void airscope_auth_revoke_all_sessions(void);

esp_err_t airscope_auth_create_automation_token(
    const char *label, char token_id[AIRSCOPE_TOKEN_ID_LEN + 1],
    char plaintext[AIRSCOPE_AUTOMATION_TOKEN_LEN + 1]);
bool airscope_auth_validate_bearer(const char *plaintext);
esp_err_t airscope_auth_revoke_automation_token(const char *token_id);
size_t airscope_auth_list_tokens(airscope_token_summary_t *out, size_t capacity);
esp_err_t airscope_auth_revoke_all_tokens(void);

esp_err_t airscope_auth_generate_secret(char *out, size_t out_size, size_t length);

#ifdef __cplusplus
}
#endif
