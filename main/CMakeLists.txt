idf_component_register(
    SRCS "airscope_main.c"
    INCLUDE_DIRS "."
    REQUIRES airscope_api airscope_auth airscope_board airscope_config airscope_display
             airscope_events airscope_wifi nvs_flash esp_event esp_netif
)
