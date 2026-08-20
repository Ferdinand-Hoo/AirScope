idf_component_register(
    SRCS "airscope_display.c"
    INCLUDE_DIRS "include"
    REQUIRES airscope_board airscope_config airscope_events airscope_wifi lvgl
)
