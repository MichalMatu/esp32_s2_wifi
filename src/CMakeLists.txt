set(srcs sta2eth_main.c diagnostics.c)

if(NOT CONFIG_APP_OLED_DEBUG_ONLY)
    list(APPEND srcs usb_ncm_iface.c manual_config.c)
endif()

idf_component_register(SRCS ${srcs}
                       PRIV_REQUIRES esp_wifi esp_timer esp_eth esp_http_server
                                     nvs_flash esp_driver_gpio esp_driver_i2c
                       INCLUDE_DIRS "")
