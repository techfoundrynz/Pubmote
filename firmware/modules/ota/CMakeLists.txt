idf_component_register(
    SRCS "src/update_client.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES config espressif__cjson esp_http_client app_update mbedtls)
