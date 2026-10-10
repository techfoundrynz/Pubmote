idf_component_register(
    SRCS
        "src/callback_registry.c"
        "src/conversion_utils.c"
        "src/diagnostic_log.c"
        "src/internal_ram_reserve.c"
        "src/log_ring.c"
        "src/mem_debug.c"
        "src/number_utils.c"
        "src/vesc_utils.c"
    INCLUDE_DIRS "include"
    REQUIRES config freertos heap log)

if(CONFIG_SPIRAM_USE_MALLOC)
    # Retain the protected internal pool for DMA, task stacks and libc locks.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_psram_extram_reserve_dma_pool")
endif()
