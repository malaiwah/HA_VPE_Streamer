#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t ver;
    uint8_t kind;
    uint16_t seq;
    uint32_t nbytes;
} __attribute__((packed)) ws_frame_header_t;

typedef struct {
    size_t len;
    uint8_t data[960];
} ws_audio_pcm_frame_t;

esp_err_t ws_audio_init(size_t frame_bytes);
esp_err_t ws_audio_start(const char *url, const char *token);
void ws_audio_stop(void);
bool ws_audio_is_connected(void);
QueueHandle_t ws_audio_get_rx_queue(void);
QueueHandle_t ws_audio_get_tx_queue(void);
esp_err_t ws_audio_queue_tx(const uint8_t *pcm, size_t len);
void ws_audio_notify_mode(const char *mode);

#ifdef __cplusplus
}
#endif

