#include "ledring.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"

#define TAG "ledring"

static rmt_channel_handle_t s_rmt_channel;
static rmt_encoder_handle_t s_led_encoder;
static uint8_t *s_pixels;
static int s_led_count;
static int s_power_pin = -1;
static ledring_state_t s_state = LEDRING_STATE_BOOT;
static int s_volume_percent = 50;
static float s_vu_level = 0.0f;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_lock;

static void ledring_task(void *arg);

static inline void set_pixel(int idx, uint32_t rgb)
{
    if (!s_pixels || idx < 0 || idx >= s_led_count) {
        return;
    }
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    s_pixels[idx * 3 + 0] = g;
    s_pixels[idx * 3 + 1] = r;
    s_pixels[idx * 3 + 2] = b;
}

static void fill_color(uint32_t rgb)
{
    for (int i = 0; i < s_led_count; ++i) {
        set_pixel(i, rgb);
    }
}

static void render_boot(int tick)
{
    fill_color(0x000010);
    int pos = (tick / 2) % s_led_count;
    set_pixel(pos, 0x0000FF);
}

static void render_connecting(int tick)
{
    float phase = (float)(tick % 40) / 40.0f;
    uint8_t brightness = (uint8_t)(20 + 80 * (0.5f - 0.5f * cosf(phase * 2.0f * 3.14159f)));
    uint32_t color = ((uint32_t)brightness << 16) | ((uint32_t)brightness << 8) | brightness;
    fill_color(color);
}

static void render_idle(void)
{
    fill_color(0x101010);
}

static void render_muted(void)
{
    fill_color(0xFF0000);
}

static void render_error(int tick)
{
    fill_color((tick % 10) < 5 ? 0xFF4500 : 0x202000);
}

static void render_volume_bar(void)
{
    int active = (s_volume_percent * s_led_count) / 100;
    for (int i = 0; i < s_led_count; ++i) {
        if (i < active) {
            set_pixel(i, 0x0040FF);
        } else {
            set_pixel(i, 0x050505);
        }
    }
}

static void render_vu(float level, uint32_t color)
{
    int active = (int)(level * s_led_count);
    if (active > s_led_count) {
        active = s_led_count;
    }
    for (int i = 0; i < s_led_count; ++i) {
        if (i < active) {
            set_pixel(i, color);
        } else {
            set_pixel(i, 0x040404);
        }
    }
}

static void ledring_draw(int tick)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ledring_state_t state = s_state;
    float vu = s_vu_level;
    xSemaphoreGive(s_lock);

    switch (state) {
    case LEDRING_STATE_BOOT:
        render_boot(tick);
        break;
    case LEDRING_STATE_WIFI_CONNECTING:
        render_connecting(tick);
        break;
    case LEDRING_STATE_ONLINE_IDLE:
        render_idle();
        render_volume_bar();
        break;
    case LEDRING_STATE_STREAMING_TX:
        render_vu(vu, 0x00FF00);
        break;
    case LEDRING_STATE_STREAMING_RX:
        render_vu(vu, 0x0000FF);
        break;
    case LEDRING_STATE_MUTED:
        render_muted();
        break;
    case LEDRING_STATE_ERROR:
        render_error(tick);
        break;
    }

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };
    rmt_transmit(s_rmt_channel, s_led_encoder, s_pixels, s_led_count * 3, &tx_cfg);
    rmt_tx_wait_all_done(s_rmt_channel, portMAX_DELAY);
}

static void ledring_task(void *arg)
{
    int tick = 0;
    while (1) {
        ledring_draw(tick++);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t ledring_init(int data_pin, int power_pin, int led_count)
{
    if (led_count <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_led_count = led_count;
    s_power_pin = power_pin;
    s_pixels = calloc(led_count, 3);
    ESP_RETURN_ON_FALSE(s_pixels != NULL, ESP_ERR_NO_MEM, TAG, "pixel alloc");

    if (s_power_pin >= 0) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_power_pin,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
        gpio_set_level(s_power_pin, 1);
    }

    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = data_pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .mem_block_symbols = 64,
        .resolution_hz = 10 * 1000 * 1000,
        .trans_queue_depth = 4,
        .flags = {
            .with_dma = false,
        },
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &s_rmt_channel), TAG, "new tx channel");

    led_strip_encoder_config_t encoder_cfg = {
        .resolution = 10 * 1000 * 1000,
    };
    ESP_RETURN_ON_ERROR(led_strip_new_encoder(&encoder_cfg, &s_led_encoder), TAG, "encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_rmt_channel), TAG, "rmt enable");

    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "lock");
    return ESP_OK;
}

void ledring_start(void)
{
    if (!s_task) {
        xTaskCreate(ledring_task, "ledring", 4096, NULL, 4, &s_task);
    }
}

void ledring_set_state(ledring_state_t state)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state = state;
    xSemaphoreGive(s_lock);
}

void ledring_set_volume(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    s_volume_percent = percent;
}

void ledring_set_vu(float level)
{
    if (!s_lock) {
        return;
    }
    if (level < 0.0f) {
        level = 0.0f;
    }
    if (level > 1.0f) {
        level = 1.0f;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_vu_level = level;
    xSemaphoreGive(s_lock);
}

