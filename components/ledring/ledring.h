#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LEDRING_STATE_BOOT,
    LEDRING_STATE_WIFI_CONNECTING,
    LEDRING_STATE_ONLINE_IDLE,
    LEDRING_STATE_STREAMING_TX,
    LEDRING_STATE_STREAMING_RX,
    LEDRING_STATE_MUTED,
    LEDRING_STATE_ERROR,
} ledring_state_t;

esp_err_t ledring_init(int data_pin, int power_pin, int led_count);
void ledring_start(void);
void ledring_set_state(ledring_state_t state);
void ledring_set_volume(int percent);
void ledring_set_vu(float level);

#ifdef __cplusplus
}
#endif

