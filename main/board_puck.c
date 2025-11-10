#include "board_puck.h"

#define PIN_VAL(option, fallback) ((option) >= 0 ? (option) : (fallback))

static const board_pins_t pins = {
    .mic_bclk = CONFIG_PUCK_MIC_BCLK_PIN,
    .mic_ws = CONFIG_PUCK_MIC_WS_PIN,
    .mic_data_in = CONFIG_PUCK_MIC_DATA_PIN,
    .spk_bclk = CONFIG_PUCK_SPK_BCLK_PIN,
    .spk_ws = CONFIG_PUCK_SPK_WS_PIN,
    .spk_data_out = CONFIG_PUCK_SPK_DATA_PIN,
    .encoder_a = CONFIG_PUCK_ENCODER_A_PIN,
    .encoder_b = CONFIG_PUCK_ENCODER_B_PIN,
    .encoder_sw = CONFIG_PUCK_ENCODER_SW_PIN,
    .button_k0 = CONFIG_PUCK_BUTTON_K0_PIN,
    .mic_switch = CONFIG_PUCK_MIC_SWITCH_PIN,
    .led_data = CONFIG_PUCK_LED_DATA_PIN,
    .led_power = CONFIG_PUCK_LED_PWR_PIN,
};

const board_pins_t *board_get_pins(void)
{
    return &pins;
}

