#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "airscope_api.h"
#include "airscope_auth.h"
#include "airscope_board.h"
#include "airscope_config.h"
#include "airscope_config_store.h"
#include "airscope_display.h"
#include "airscope_events.h"
#include "airscope_wifi.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "airscope";

static esp_err_t initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        err = nvs_flash_init();
    }
    return err;
}

static void startup_check(esp_err_t error, const char *stage)
{
    if (error == ESP_OK) {
        return;
    }

    ESP_LOGE(TAG, "Startup failed at %s: %s (0x%x)", stage,
             esp_err_to_name(error), (unsigned int)error);
    airscope_display_show_error(stage, error);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    startup_check(initialize_nvs(), "Non-volatile storage");
    startup_check(airscope_board_init(), "Board display");
    airscope_display_show_initializing("Checking configuration");

    uint32_t boot_id = esp_random();
    startup_check(airscope_events_init(boot_id), "Runtime events");
    airscope_events_record(AIRSCOPE_EVENT_INFO, "boot.started", "{}");

    bool recovery = airscope_board_recovery_requested(5000);
    char management_password[AIRSCOPE_PASSWORD_MAX_LEN + 1] = {0};
    bool management_password_generated = false;
    startup_check(airscope_auth_init(management_password, sizeof(management_password),
                                     &management_password_generated),
                  "Management credentials");

    uint8_t mac[6];
    startup_check(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), "Device identity");
    airscope_ap_config_t config;
    bool found = false;
    esp_err_t load_err = airscope_config_store_load(&config, &found);
    if (found && load_err == ESP_OK && config.schema_version == 1) {
        config.schema_version = AIRSCOPE_CONFIG_SCHEMA_VERSION;
        if (config.primary_channel == 6) {
            config.primary_channel = 1;
            config.bandwidth = AIRSCOPE_BANDWIDTH_HT20;
            config.secondary_channel = AIRSCOPE_SECONDARY_NONE;
        }
        startup_check(airscope_config_store_save(&config), "Configuration migration");
    }
    airscope_validation_result_t validation =
        found && load_err == ESP_OK ? airscope_config_validate(&config, true)
                                    : (airscope_validation_result_t){.valid = false};

    bool provisioning = false;
    if (recovery || !found || load_err != ESP_OK || !validation.valid) {
        airscope_config_make_default(&config, mac);
        if (recovery) {
            startup_check(airscope_auth_reset(management_password,
                                              sizeof(management_password)),
                          "Credential recovery");
            startup_check(airscope_config_store_erase(), "Configuration recovery");
            airscope_events_record(AIRSCOPE_EVENT_WARNING, "recovery.completed",
                                   "{\"credentialsRotated\":true}");
        }
        startup_check(airscope_config_store_save(&config), "Configuration storage");
        provisioning = true;
    } else if (management_password_generated) {
        provisioning = true;
    }

    airscope_display_show_initializing("Starting access point");
    startup_check(esp_netif_init(), "Network stack");
    startup_check(esp_event_loop_create_default(), "System event loop");
    startup_check(airscope_wifi_init(&config), "WiFi access point");
    startup_check(airscope_api_start(), "HTTPS management");

    const airscope_display_start_options_t display_options = {
        .management_password = management_password,
        .show_provisioning = provisioning,
    };
    startup_check(airscope_display_start(&config, &display_options), "Status display");
    explicit_bzero(management_password, sizeof(management_password));
    ESP_LOGI(TAG, "AirScope ready: SSID=%s management=https://192.168.4.1", config.ssid);
}
