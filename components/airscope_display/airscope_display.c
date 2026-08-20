#include "airscope_display.h"

#include <stdio.h>
#include <string.h>

#include "airscope_board.h"
#include "airscope_wifi.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#define PROVISIONING_WINDOW_MS (10U * 60U * 1000U)

static lv_obj_t *s_client_value;
static lv_obj_t *s_operation_value;
static lv_obj_t *s_tabview;
static lv_obj_t *s_provisioning_page;
static lv_timer_t *s_provisioning_timer;
static lv_obj_t *s_ssid_value;
static lv_obj_t *s_channel_value;
static lv_obj_t *s_security_value;

static bool append_qr_value(char *payload, size_t payload_size, size_t *offset,
                            const char *value)
{
    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\' || *cursor == ';' || *cursor == ',' ||
            *cursor == ':' || *cursor == '"') {
            if (*offset + 1 >= payload_size) {
                return false;
            }
            payload[(*offset)++] = '\\';
        }
        if (*offset + 1 >= payload_size) {
            return false;
        }
        payload[(*offset)++] = *cursor;
    }
    payload[*offset] = '\0';
    return true;
}

static bool make_wifi_qr_payload(const airscope_ap_config_t *config,
                                 char *payload, size_t payload_size)
{
    const bool open = config->auth_mode == AIRSCOPE_AUTH_OPEN;
    int written = snprintf(payload, payload_size, "WIFI:T:%s;S:",
                           open ? "nopass" : "WPA");
    if (written < 0 || (size_t)written >= payload_size) {
        return false;
    }
    size_t offset = (size_t)written;
    if (!append_qr_value(payload, payload_size, &offset, config->ssid)) {
        return false;
    }
    if (!open) {
        written = snprintf(payload + offset, payload_size - offset, ";P:");
        if (written < 0 || (size_t)written >= payload_size - offset) {
            return false;
        }
        offset += (size_t)written;
        if (!append_qr_value(payload, payload_size, &offset,
                             config->ap_password)) {
            return false;
        }
    }
    written = snprintf(payload + offset, payload_size - offset, ";H:%s;;",
                       config->ssid_hidden ? "true" : "false");
    return written >= 0 && (size_t)written < payload_size - offset;
}

static lv_obj_t *set_label_text(lv_obj_t *parent, const char *caption,
                                const char *value, int y)
{
    lv_obj_t *caption_label = lv_label_create(parent);
    lv_label_set_text(caption_label, caption);
    lv_obj_set_style_text_color(caption_label, lv_color_hex(0x7B8794), 0);
    lv_obj_align(caption_label, LV_ALIGN_TOP_LEFT, 16, y);

    lv_obj_t *value_label = lv_label_create(parent);
    lv_label_set_text(value_label, value);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xE9EEF2), 0);
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, -16, y);
    return value_label;
}

static void set_provisioning_field(lv_obj_t *parent, const char *caption,
                                   const char *value, int y)
{
    lv_obj_t *caption_label = lv_label_create(parent);
    lv_label_set_text(caption_label, caption);
    lv_obj_set_width(caption_label, 216);
    lv_obj_set_style_text_font(caption_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(caption_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(caption_label, lv_color_hex(0x7B8794), 0);
    lv_obj_align(caption_label, LV_ALIGN_TOP_MID, 0, y);

    lv_obj_t *value_label = lv_label_create(parent);
    lv_label_set_text(value_label, value);
    lv_obj_set_width(value_label, 216);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xE9EEF2), 0);
    lv_obj_align(value_label, LV_ALIGN_TOP_MID, 0, y + 16);
}

static void status_timer(lv_timer_t *timer)
{
    (void)timer;
    airscope_ap_config_t config;
    airscope_wifi_get_applied_config(&config);
    if (s_ssid_value != NULL) {
        lv_label_set_text(s_ssid_value, config.ssid);
    }
    if (s_channel_value != NULL) {
        lv_label_set_text_fmt(s_channel_value, "%u / %s", config.primary_channel,
                              airscope_bandwidth_name(config.bandwidth));
    }
    if (s_security_value != NULL) {
        lv_label_set_text(s_security_value,
                          airscope_auth_mode_name(config.auth_mode));
    }
    if (s_client_value != NULL) {
        lv_label_set_text_fmt(s_client_value, "%u", airscope_wifi_client_count());
    }
    if (s_operation_value != NULL) {
        airscope_operation_result_t operation = airscope_wifi_latest_operation();
        lv_label_set_text(s_operation_value,
                          operation.type == AIRSCOPE_OPERATION_NONE
                              ? "Ready"
                              : operation.success ? "Succeeded" : "Failed");
        lv_obj_set_style_text_color(
            s_operation_value,
            operation.type == AIRSCOPE_OPERATION_NONE
                ? lv_color_hex(0x9AA6B2)
                : operation.success ? lv_color_hex(0x36D399) : lv_color_hex(0xFB7185),
            0);
    }
}

static void provisioning_timeout(lv_timer_t *timer)
{
    if (s_tabview != NULL && s_provisioning_page != NULL) {
        if (lv_tabview_get_tab_active(s_tabview) == 2) {
            lv_tabview_set_active(s_tabview, 0, LV_ANIM_OFF);
        }
        lv_obj_t *tab_button = lv_tabview_get_tab_button(s_tabview, 2);
        lv_obj_delete(s_provisioning_page);
        if (tab_button != NULL) {
            lv_obj_delete(tab_button);
        }
        s_provisioning_page = NULL;
    }
    s_provisioning_timer = NULL;
    lv_timer_delete(timer);
}

void airscope_display_show_initializing(const char *message)
{
    if (airscope_board_display() == NULL || !bsp_display_lock(1000)) {
        return;
    }
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10161D), 0);
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "AirScope");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF4F7F9), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -18);
    lv_obj_t *state = lv_label_create(screen);
    lv_label_set_text(state, message != NULL ? message : "Initializing");
    lv_obj_set_style_text_color(state, lv_color_hex(0x7DD3FC), 0);
    lv_obj_align(state, LV_ALIGN_CENTER, 0, 18);
    bsp_display_unlock();
}

void airscope_display_show_error(const char *stage, esp_err_t error)
{
    if (airscope_board_display() == NULL || !bsp_display_lock(1000)) {
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10161D), 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Startup failed");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFB7185), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

    lv_obj_t *stage_label = lv_label_create(screen);
    lv_label_set_text(stage_label, stage != NULL ? stage : "Unknown stage");
    lv_obj_set_width(stage_label, LV_PCT(90));
    lv_obj_set_style_text_align(stage_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(stage_label, lv_color_hex(0xE9EEF2), 0);
    lv_obj_align(stage_label, LV_ALIGN_CENTER, 0, -8);

    char error_text[64];
    snprintf(error_text, sizeof(error_text), "%s (0x%x)",
             esp_err_to_name(error), (unsigned int)error);
    lv_obj_t *error_label = lv_label_create(screen);
    lv_label_set_text(error_label, error_text);
    lv_obj_set_style_text_color(error_label, lv_color_hex(0xFBBF24), 0);
    lv_obj_align(error_label, LV_ALIGN_CENTER, 0, 28);

    bsp_display_unlock();
}

esp_err_t airscope_display_start(const airscope_ap_config_t *config,
                                 const airscope_display_start_options_t *options)
{
    if (config == NULL || airscope_board_display() == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bsp_display_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10161D), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xE9EEF2), 0);

    lv_obj_t *tabs = lv_tabview_create(screen);
    s_tabview = tabs;
    lv_obj_set_size(tabs, LV_PCT(100), LV_PCT(100));
    lv_tabview_set_tab_bar_size(tabs, 42);
    lv_obj_set_style_bg_color(tabs, lv_color_hex(0x10161D), 0);
    lv_obj_set_style_bg_color(lv_tabview_get_tab_bar(tabs), lv_color_hex(0x18212A), 0);

    lv_obj_t *status = lv_tabview_add_tab(tabs, "AP");
    lv_obj_set_style_bg_color(status, lv_color_hex(0x10161D), 0);
    lv_obj_t *title = lv_label_create(status);
    lv_label_set_text(title, config->ssid);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 6);
    s_ssid_value = title;

    char channel[24];
    snprintf(channel, sizeof(channel), "%u / %s", config->primary_channel,
             airscope_bandwidth_name(config->bandwidth));
    s_channel_value = set_label_text(status, "Channel", channel, 44);
    s_security_value =
        set_label_text(status, "Security",
                       airscope_auth_mode_name(config->auth_mode), 72);
    set_label_text(status, "Manage", "192.168.4.1", 100);

    lv_obj_t *client_caption = lv_label_create(status);
    lv_label_set_text(client_caption, "Clients");
    lv_obj_set_style_text_color(client_caption, lv_color_hex(0x7B8794), 0);
    lv_obj_align(client_caption, LV_ALIGN_TOP_LEFT, 16, 128);
    s_client_value = lv_label_create(status);
    lv_label_set_text(s_client_value, "0");
    lv_obj_align(s_client_value, LV_ALIGN_TOP_RIGHT, -16, 128);

    lv_obj_t *operation = lv_tabview_add_tab(tabs, "Result");
    lv_obj_set_style_bg_color(operation, lv_color_hex(0x10161D), 0);
    lv_obj_t *operation_title = lv_label_create(operation);
    lv_label_set_text(operation_title, "Latest operation");
    lv_obj_set_style_text_font(operation_title, &lv_font_montserrat_18, 0);
    lv_obj_align(operation_title, LV_ALIGN_TOP_LEFT, 12, 10);
    s_operation_value = lv_label_create(operation);
    lv_label_set_text(s_operation_value, "Ready");
    lv_obj_set_style_text_font(s_operation_value, &lv_font_montserrat_24, 0);
    lv_obj_align(s_operation_value, LV_ALIGN_CENTER, 0, -12);

    if (options != NULL && options->show_provisioning) {
        lv_obj_t *provisioning = lv_tabview_add_tab(tabs, "Setup");
        s_provisioning_page = provisioning;
        lv_obj_set_style_bg_color(provisioning, lv_color_hex(0x10161D), 0);
        lv_obj_t *provision_title = lv_label_create(provisioning);
        lv_label_set_text(provision_title, "AP broadcasting");
        lv_obj_set_style_text_font(provision_title, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(provision_title, lv_color_hex(0x36D399), 0);
        lv_obj_align(provision_title, LV_ALIGN_TOP_MID, 0, 0);

        set_provisioning_field(provisioning, "Network name (SSID)", config->ssid, 22);

        char qr_payload[256];
        if (make_wifi_qr_payload(config, qr_payload, sizeof(qr_payload))) {
            lv_obj_t *qr = lv_qrcode_create(provisioning);
            lv_qrcode_set_size(qr, 72);
            lv_qrcode_set_dark_color(qr, lv_color_hex(0x10161D));
            lv_qrcode_set_light_color(qr, lv_color_hex(0xF4F7F9));
            lv_qrcode_set_quiet_zone(qr, true);
            lv_qrcode_update(qr, qr_payload, strlen(qr_payload));
            lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 58);
        }
        explicit_bzero(qr_payload, sizeof(qr_payload));

        set_provisioning_field(
            provisioning,
            config->auth_mode == AIRSCOPE_AUTH_OPEN ? "WiFi security" : "WiFi password",
            config->auth_mode == AIRSCOPE_AUTH_OPEN ? "Open (no password)"
                                                    : config->ap_password,
            134);
        set_provisioning_field(
            provisioning, "Admin password",
            options->management_password != NULL ? options->management_password : "", 169);
        lv_obj_t *hint = lv_label_create(provisioning);
        lv_label_set_text(hint, "Credentials hide after 10 min");
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(0xFBBF24), 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
        s_provisioning_timer =
            lv_timer_create(provisioning_timeout, PROVISIONING_WINDOW_MS, NULL);
        lv_tabview_set_active(tabs, 2, LV_ANIM_OFF);
    }
    lv_timer_create(status_timer, 1000, NULL);
    bsp_display_unlock();
    return ESP_OK;
}
