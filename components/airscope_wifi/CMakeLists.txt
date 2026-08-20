idf_component_register(
    SRCS "airscope_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES airscope_config airscope_events esp_wifi esp_netif esp_event lwip
)
