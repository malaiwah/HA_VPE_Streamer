#pragma once

#include "sdkconfig.h"

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
    int encoder_a;
    int encoder_b;
    int encoder_sw;
    int button_k0;
    int mic_switch;
    int led_data;
    int led_power;
} board_pins_t;

const board_pins_t *board_get_pins(void);

#ifdef __cplusplus
}
#endif

