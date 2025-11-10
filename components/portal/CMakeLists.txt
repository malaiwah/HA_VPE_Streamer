idf_component_register(SRCS "portal.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_event esp_netif esp_wifi esp_http_server nvs_flash)
