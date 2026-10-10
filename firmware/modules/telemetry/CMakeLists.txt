idf_component_register(
    SRCS "src/stats.c"
    INCLUDE_DIRS "include"
    REQUIRES config
    PRIV_REQUIRES freertos)
