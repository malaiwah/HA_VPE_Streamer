#pragma once

#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int mic_bclk;
    int mic_ws;
    int mic_data_in;
    int spk_bclk;
    int spk_ws;
    int spk_data_out;
} audio_pins_t;

esp_err_t audio_init(const audio_pins_t *pins);
size_t audio_read(int16_t *samples, size_t sample_count, TickType_t timeout);
size_t audio_write(const int16_t *samples, size_t sample_count, TickType_t timeout);
void audio_set_mic_gain(float gain);
void audio_set_spk_gain(float gain);
float audio_get_mic_gain(void);
float audio_get_spk_gain(void);

#ifdef __cplusplus
}
#endif

