if(ESP_PLATFORM)
    idf_component_register(SRCS "tab5_app_boot.c" INCLUDE_DIRS "include"
                           REQUIRES app_update esp_app_format esp_partition spi_flash nvs_flash)
else()
    idf_component_register(SRCS "tab5_app_boot_sim.c" INCLUDE_DIRS "include")
endif()
