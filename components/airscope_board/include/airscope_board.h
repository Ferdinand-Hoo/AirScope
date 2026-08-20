#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t airscope_board_init(void);
lv_display_t *airscope_board_display(void);
bool airscope_board_recovery_requested(uint32_t hold_ms);
esp_err_t airscope_board_set_brightness(uint8_t percent);
uint8_t airscope_board_get_brightness(void);

#ifdef __cplusplus
}
#endif
