idf_component_register(
    SRCS "src/game_store.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES espressif__cjson joltwallet__littlefs console mbedtls)
