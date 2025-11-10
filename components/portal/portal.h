#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[33];
    char password[65];
    char server_url[128];
    char token[128];
    char mode[16];
    int vol_spk;
    int vol_mic;
} portal_config_t;

typedef struct {
    char ssid[33];
    char password[65];
    char server_url[128];
    char token[128];
    char mode[16];
    int sample_rate_in;
    int sample_rate_out;
} portal_result_t;

esp_err_t portal_run(portal_config_t *defaults, portal_result_t *out_result);
void portal_stop(void);

#ifdef __cplusplus
}
#endif

