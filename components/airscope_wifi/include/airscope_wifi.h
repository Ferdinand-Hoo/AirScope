#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "airscope_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIRSCOPE_OPERATION_ID_LEN 16
#define AIRSCOPE_OPERATION_MESSAGE_LEN 127
#define AIRSCOPE_MAX_CLIENTS 10

typedef enum {
    AIRSCOPE_OPERATION_NONE = 0,
    AIRSCOPE_OPERATION_CONFIG_TRANSACTION,
    AIRSCOPE_OPERATION_CHANNEL_SWITCH,
} airscope_operation_type_t;

typedef struct {
    char id[AIRSCOPE_OPERATION_ID_LEN + 1];
    airscope_operation_type_t type;
    bool success;
    bool rollback_attempted;
    bool rollback_succeeded;
    esp_err_t error;
    char message[AIRSCOPE_OPERATION_MESSAGE_LEN + 1];
} airscope_operation_result_t;

typedef struct {
    uint8_t primary_channel;
    airscope_bandwidth_t bandwidth;
    airscope_secondary_channel_t secondary_channel;
    uint8_t csa_count;
} airscope_channel_request_t;

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
} airscope_client_t;

esp_err_t airscope_wifi_init(const airscope_ap_config_t *initial_config);
bool airscope_wifi_is_started(void);
bool airscope_wifi_is_degraded(void);
void airscope_wifi_get_applied_config(airscope_ap_config_t *out);
airscope_operation_result_t airscope_wifi_apply_config(
    const airscope_ap_config_t *proposed);
airscope_operation_result_t airscope_wifi_switch_channel(
    const airscope_channel_request_t *request);
airscope_operation_result_t airscope_wifi_latest_operation(void);
size_t airscope_wifi_get_clients(airscope_client_t *out, size_t capacity);
uint8_t airscope_wifi_client_count(void);

#ifdef __cplusplus
}
#endif
