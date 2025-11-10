#include "audio.h"

#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2s_std.h"

#define TAG "audio"

static i2s_chan_handle_t s_tx_chan;
static i2s_chan_handle_t s_rx_chan;
static float s_mic_gain = 1.0f;
static float s_spk_gain = 1.0f;

static inline int16_t apply_gain(int16_t sample, float gain)
{
    int32_t val = (int32_t)((float)sample * gain);
    if (val > INT16_MAX) {
        val = INT16_MAX;
    } else if (val < INT16_MIN) {
        val = INT16_MIN;
    }
    return (int16_t)val;
}

esp_err_t audio_init(const audio_pins_t *pins)
{
    if (!pins) {
        return ESP_ERR_INVALID_ARG;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan), TAG, "new channel");

    i2s_std_config_t rx_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = pins->mic_bclk,
            .ws = pins->mic_ws,
            .dout = I2S_GPIO_UNUSED,
            .din = pins->mic_data_in,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    i2s_std_config_t tx_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = pins->spk_bclk,
            .ws = pins->spk_ws,
            .dout = pins->spk_data_out,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx_chan, &rx_cfg), TAG, "rx init");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &tx_cfg), TAG, "tx init");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "rx enable");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "tx enable");

    return ESP_OK;
}

size_t audio_read(int16_t *samples, size_t sample_count, TickType_t timeout)
{
    if (!s_rx_chan || !samples || sample_count == 0) {
        return 0;
    }
    size_t bytes_read = 0;
    if (i2s_channel_read(s_rx_chan, samples, sample_count * sizeof(int16_t), &bytes_read, timeout) == ESP_OK) {
        size_t count = bytes_read / sizeof(int16_t);
        for (size_t i = 0; i < count; ++i) {
            samples[i] = apply_gain(samples[i], s_mic_gain);
        }
        return count;
    }
    return 0;
}

size_t audio_write(const int16_t *samples, size_t sample_count, TickType_t timeout)
{
    if (!s_tx_chan || !samples || sample_count == 0) {
        return 0;
    }
    size_t bytes = sample_count * sizeof(int16_t);
    int16_t *buf = malloc(bytes);
    if (!buf) {
        return 0;
    }
    for (size_t i = 0; i < sample_count; ++i) {
        buf[i] = apply_gain(samples[i], s_spk_gain);
    }
    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(s_tx_chan, buf, bytes, &bytes_written, timeout);
    free(buf);
    if (err == ESP_OK) {
        return bytes_written / sizeof(int16_t);
    }
    return 0;
}

void audio_set_mic_gain(float gain)
{
    s_mic_gain = gain;
}

void audio_set_spk_gain(float gain)
{
    s_spk_gain = gain;
}

float audio_get_mic_gain(void)
{
    return s_mic_gain;
}

float audio_get_spk_gain(void)
{
    return s_spk_gain;
}

