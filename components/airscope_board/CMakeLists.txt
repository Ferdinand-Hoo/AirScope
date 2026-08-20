idf_component_register(
    SRCS "airscope_board.c"
    INCLUDE_DIRS "include"
    REQUIRES waveshare__esp32_s3_touch_lcd_1_69 esp_driver_gpio esp_timer
)
