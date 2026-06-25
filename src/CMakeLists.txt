idf_component_register(SRCS sta2eth_main.c
                            usb_ncm_iface.c
                            manual_config.c
                            config_access.c
                            status_led.c
                            wifi_profiles.c
                            debug_console.c
                       PRIV_REQUIRES esp_wifi esp_timer esp_eth esp_http_server
                                     nvs_flash esp_driver_gpio app_update esp_event esp_netif
                                     spi_flash esp_hw_support espcoredump esp_partition
                                     esp_driver_uart mdns
                       INCLUDE_DIRS "")
