idf_component_register(
    SRCS "airscope_config.c" "airscope_config_store.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_hw_support
)
