idf_component_register(SRCS sta2eth_main.c
                            usb_ncm_iface.c
                            manual_config.c
                            config_access.c
                            config_access_policy.c
                            status_led.c
                            wifi_profiles.c
                            debug_console.c
                       PRIV_REQUIRES esp_wifi esp_timer esp_eth esp_http_server
                                     nvs_flash esp_driver_gpio app_update esp_event esp_netif
                                     spi_flash esp_hw_support espcoredump esp_partition
                                     esp_driver_uart mdns
                       INCLUDE_DIRS "")

# profiles_handler formats indexes from a store capped at WIFI_PROFILE_MAX (8),
# but GCC cannot infer that cross-TU bound and reports a false-positive
# -Wformat-truncation for the existing 8-byte id buffer. Keep the warning
# visible while preventing this one source file from failing the IDF build.
set_source_files_properties(manual_config.c PROPERTIES COMPILE_OPTIONS "-Wno-error=format-truncation")
