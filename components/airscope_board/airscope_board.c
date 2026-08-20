#include "airscope_board.h"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RECOVERY_GPIO GPIO_NUM_40

static lv_display_t *s_display;
static uint8_t s_brightness = 80;

esp_err_t airscope_board_init(void)
{
    gpio_config_t recovery = {
        .pin_bit_mask = 1ULL << RECOVERY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&recovery), "airscope_board",
                        "recovery GPIO configuration failed");

    s_display = bsp_display_start();
    if (s_display == NULL) {
        return ESP_FAIL;
    }
    ESP_RETURN_ON_ERROR(bsp_display_brightness_set(s_brightness), "airscope_board",
                        "backlight configuration failed");
    return ESP_OK;
}

lv_display_t *airscope_board_display(void)
{
    return s_display;
}

bool airscope_board_recovery_requested(uint32_t hold_ms)
{
    if (gpio_get_level(RECOVERY_GPIO) != 0) {
        return false;
    }
    int64_t started_at = esp_timer_get_time();
    while (gpio_get_level(RECOVERY_GPIO) == 0) {
        if ((esp_timer_get_time() - started_at) / 1000 >= hold_ms) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    return false;
}

esp_err_t airscope_board_set_brightness(uint8_t percent)
{
    s_brightness = percent > 100 ? 100 : percent;
    return bsp_display_brightness_set(s_brightness);
}

uint8_t airscope_board_get_brightness(void)
{
    return s_brightness;
}
