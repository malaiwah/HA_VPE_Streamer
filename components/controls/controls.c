#include "controls.h"

#include <string.h>

#include "driver/gpio.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "controls"

static controls_pins_t s_pins;
static controls_callbacks_t s_callbacks;
static TaskHandle_t s_task;
static int s_volume = 50;
static bool s_muted;
static volatile bool s_button_pressed;
static char s_mode[16] = "always_on";

static void controls_task(void *arg)
{
    bool last_button = true;
    bool last_encoder_sw = true;
    bool last_mic_switch = true;
    int last_encoder_a = 1;
    int last_encoder_b = 1;
    int64_t button_press_time = 0;
    int64_t encoder_press_time = 0;

    while (1) {
        bool button = gpio_get_level(s_pins.button_k0);
        bool enc_sw = gpio_get_level(s_pins.encoder_sw);
        bool mic_sw = gpio_get_level(s_pins.mic_switch);
        int enc_a = gpio_get_level(s_pins.encoder_a);
        int enc_b = gpio_get_level(s_pins.encoder_b);

        s_button_pressed = !button;

        if (button != last_button) {
            if (!button) {
                button_press_time = esp_timer_get_time();
            } else {
                int64_t duration = esp_timer_get_time() - button_press_time;
                if (duration > 2000000) {
                    if (strcmp(s_mode, "always_on") == 0) {
                        controls_set_mode("ptt");
                        if (s_callbacks.mode_cb) {
                            s_callbacks.mode_cb(s_mode, s_callbacks.user_ctx);
                        }
                    } else {
                        controls_set_mode("always_on");
                        if (s_callbacks.mode_cb) {
                            s_callbacks.mode_cb(s_mode, s_callbacks.user_ctx);
                        }
                    }
                } else {
                    controls_set_muted(!s_muted);
                    if (s_callbacks.mute_cb) {
                        s_callbacks.mute_cb(s_muted, s_callbacks.user_ctx);
                    }
                }
            }
            last_button = button;
        }

        if (enc_sw != last_encoder_sw) {
            if (!enc_sw) {
                encoder_press_time = esp_timer_get_time();
            } else {
                int64_t duration = esp_timer_get_time() - encoder_press_time;
                if (duration < 1500000) {
                    if (s_callbacks.test_cb) {
                        s_callbacks.test_cb(s_callbacks.user_ctx);
                    }
                }
            }
            last_encoder_sw = enc_sw;
        }

        if (mic_sw != last_mic_switch) {
            if (s_callbacks.privacy_cb) {
                s_callbacks.privacy_cb(!mic_sw, s_callbacks.user_ctx);
            }
            last_mic_switch = mic_sw;
        }

        if (enc_a != last_encoder_a) {
            if (enc_a == enc_b) {
                controls_set_volume(s_volume + 1);
            } else {
                controls_set_volume(s_volume - 1);
            }
            if (s_callbacks.volume_cb) {
                s_callbacks.volume_cb(s_volume, s_callbacks.user_ctx);
            }
            last_encoder_a = enc_a;
        }
        last_encoder_b = enc_b;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void configure_gpio(int pin)
{
    if (pin < 0) {
        return;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

esp_err_t controls_init(const controls_pins_t *pins, const controls_callbacks_t *callbacks)
{
    if (!pins) {
        return ESP_ERR_INVALID_ARG;
    }
    s_pins = *pins;
    if (callbacks) {
        s_callbacks = *callbacks;
    } else {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
    }

    configure_gpio(s_pins.button_k0);
    configure_gpio(s_pins.encoder_a);
    configure_gpio(s_pins.encoder_b);
    configure_gpio(s_pins.encoder_sw);
    configure_gpio(s_pins.mic_switch);

    if (!s_task) {
        xTaskCreate(controls_task, "controls", 4096, NULL, 5, &s_task);
    }
    return ESP_OK;
}

void controls_set_volume(int volume)
{
    if (volume < 0) {
        volume = 0;
    }
    if (volume > 100) {
        volume = 100;
    }
    s_volume = volume;
}

int controls_get_volume(void)
{
    return s_volume;
}

void controls_set_mode(const char *mode)
{
    if (mode) {
        strlcpy(s_mode, mode, sizeof(s_mode));
    }
}

const char *controls_get_mode(void)
{
    return s_mode;
}

void controls_set_muted(bool muted)
{
    s_muted = muted;
}

bool controls_get_muted(void)
{
    return s_muted;
}

bool controls_button_is_pressed(void)
{
    return s_button_pressed;
}

