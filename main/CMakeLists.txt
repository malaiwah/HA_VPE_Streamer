idf_component_register(SRCS "main.c" "board_puck.c"
                    INCLUDE_DIRS "."
                    REQUIRES portal ws_audio audio controls ledring nvs_flash esp_event esp_netif esp_wifi esp_http_server json freertos esp_common)
