idf_component_register(SRCS "ws_audio.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_websocket_client esp_event esp_timer freertos nvs_flash esp_common json)
