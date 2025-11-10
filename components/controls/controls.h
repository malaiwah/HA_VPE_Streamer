#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int button_k0;
    int mic_switch;
    int encoder_a;
    int encoder_b;
    int encoder_sw;
} controls_pins_t;

typedef void (*controls_mute_cb_t)(bool muted, void *user);
typedef void (*controls_mode_cb_t)(const char *mode, void *user);
typedef void (*controls_volume_cb_t)(int volume, void *user);
typedef void (*controls_test_cb_t)(void *user);
typedef void (*controls_privacy_cb_t)(bool privacy, void *user);

typedef struct {
    controls_mute_cb_t mute_cb;
    controls_mode_cb_t mode_cb;
    controls_volume_cb_t volume_cb;
    controls_test_cb_t test_cb;
    controls_privacy_cb_t privacy_cb;
    void *user_ctx;
} controls_callbacks_t;

esp_err_t controls_init(const controls_pins_t *pins, const controls_callbacks_t *callbacks);
void controls_set_volume(int volume);
int controls_get_volume(void);
void controls_set_mode(const char *mode);
const char *controls_get_mode(void);
void controls_set_muted(bool muted);
bool controls_get_muted(void);
bool controls_button_is_pressed(void);

#ifdef __cplusplus
}
#endif

