idf_component_register(
    SRCS "airscope_api.c" "airscope_identity.c"
    INCLUDE_DIRS "include"
    REQUIRES airscope_auth airscope_config airscope_events airscope_wifi
             esp_https_server esp_timer espressif__cjson mbedtls nvs_flash
    EMBED_FILES "web/index.html.gz" "web/app.js.gz" "web/app.css.gz"
)
