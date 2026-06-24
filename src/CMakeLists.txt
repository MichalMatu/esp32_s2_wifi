idf_component_register(SRCS sta2eth_main.c
                            usb_ncm_iface.c
                            manual_config.c
                       PRIV_REQUIRES esp_wifi esp_timer esp_eth esp_http_server
                                     nvs_flash esp_driver_gpio
                       INCLUDE_DIRS "")
