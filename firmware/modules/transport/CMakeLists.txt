idf_component_register(
    SRCS "src/comms.c" "src/comms_ble.c" "src/espnow.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES support bt esp_event esp_netif esp_timer esp_wifi nvs_flash)
