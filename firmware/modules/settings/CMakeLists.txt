idf_component_register(
    SRCS "src/settings_state.c"
    INCLUDE_DIRS "include"
    REQUIRES config
    PRIV_REQUIRES freertos)
