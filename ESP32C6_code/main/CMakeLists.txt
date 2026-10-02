idf_component_register(SRCS "app_main.cpp" "sdp810.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_matter esp_driver_i2c esp_driver_gpio esp_timer nvs_flash app_update)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
