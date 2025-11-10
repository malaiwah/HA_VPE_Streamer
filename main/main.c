#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_check.h"

#include "board_puck.h"
#include "portal.h"
#include "ws_audio.h"
#include "audio.h"
#include "controls.h"
#include "ledring.h"

#define TAG "main"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define FRAME_SAMPLES 320

typedef struct {
    char ssid[33];
    char pass[65];
    char server_url[128];
    char token[128];
    char mode[16];
    int vol_spk;
    int vol_mic;
} voice_config_t;

static voice_config_t g_config;
static EventGroupHandle_t g_wifi_events;
static httpd_handle_t g_httpd;
static bool g_muted = false;
static bool g_privacy = false;
static bool g_ptt_mode = false;
static bool g_wifi_connected = false;
static esp_netif_t *g_wifi_sta;

static void audio_capture_task(void *arg);
static void audio_play_task(void *arg);
static void play_test_beep(void);

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(g_wifi_events, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(g_wifi_events, WIFI_FAIL_BIT);
        g_wifi_connected = false;
        ledring_set_state(LEDRING_STATE_WIFI_CONNECTING);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(g_wifi_events, WIFI_CONNECTED_BIT);
        g_wifi_connected = true;
        ledring_set_state(LEDRING_STATE_ONLINE_IDLE);
    }
}

static esp_err_t nvs_load_config(voice_config_t *config)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("voice", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    size_t len;
    len = sizeof(config->ssid);
    err = nvs_get_str(nvs, "ssid", config->ssid, &len);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }
    len = sizeof(config->pass);
    if (nvs_get_str(nvs, "pass", config->pass, &len) != ESP_OK) {
        config->pass[0] = '\0';
    }
    len = sizeof(config->server_url);
    if (nvs_get_str(nvs, "server", config->server_url, &len) != ESP_OK) {
        strlcpy(config->server_url, "ws://aiboss.lan.home.malaiwah.com:7000/", sizeof(config->server_url));
    }
    len = sizeof(config->token);
    if (nvs_get_str(nvs, "token", config->token, &len) != ESP_OK) {
        config->token[0] = '\0';
    }
    len = sizeof(config->mode);
    if (nvs_get_str(nvs, "mode", config->mode, &len) != ESP_OK) {
        strlcpy(config->mode, "always_on", sizeof(config->mode));
    }
    int32_t vol_spk = 80;
    nvs_get_i32(nvs, "vol_spk", &vol_spk);
    config->vol_spk = vol_spk;
    int32_t vol_mic = 100;
    nvs_get_i32(nvs, "vol_mic", &vol_mic);
    config->vol_mic = vol_mic;
    nvs_close(nvs);
    return ESP_OK;
}

static esp_err_t nvs_save_config(const voice_config_t *config)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("voice", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, "ssid", config->ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "pass", config->pass);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "server", config->server_url);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "token", config->token);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "mode", config->mode);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(nvs, "vol_spk", config->vol_spk);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(nvs, "vol_mic", config->vol_mic);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static void start_wifi_sta(const voice_config_t *config)
{
    wifi_config_t wifi_cfg = { 0 };
    strlcpy((char *)wifi_cfg.sta.ssid, config->ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, config->pass, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();
    esp_wifi_connect();
    ledring_set_state(LEDRING_STATE_WIFI_CONNECTING);
}

static esp_err_t http_root_handler(httpd_req_t *req)
{
    char resp[1024];
    wifi_ap_record_t ap_info;
    bool have_ap = esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
    snprintf(resp, sizeof(resp),
             "<html><head><title>Puck status</title></head><body><h2>Puck status</h2>"
             "<p><b>SSID:</b> %s</p>"
             "<p><b>Server:</b> %s</p>"
             "<p><b>Mode:</b> %s</p>"
             "<p><b>Volume:</b> %d</p>"
             "<p><b>Muted:</b> %s</p>"
             "<p><b>Wi-Fi:</b> %s</p>"
             "<form method='POST' action='/test'><button type='submit'>Test beep</button></form>"
             "<form method='POST' action='/reset' onsubmit='return confirm(\"Factory reset?\");'>"
             "<button type='submit'>Factory reset</button></form>"
             "</body></html>",
             g_config.ssid,
             g_config.server_url,
             g_config.mode,
             g_config.vol_spk,
             g_muted ? "yes" : "no",
             have_ap ? "connected" : "disconnected");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t http_reset_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "OK, rebooting");
    vTaskDelay(pdMS_TO_TICKS(500));
    nvs_flash_erase();
    esp_restart();
    return ESP_OK;
}

static esp_err_t http_test_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "OK");
    play_test_beep();
    return ESP_OK;
}

static void start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_start(&g_httpd, &cfg) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = http_root_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(g_httpd, &root);
        httpd_uri_t reset = {
            .uri = "/reset",
            .method = HTTP_POST,
            .handler = http_reset_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(g_httpd, &reset);
        httpd_uri_t test = {
            .uri = "/test",
            .method = HTTP_POST,
            .handler = http_test_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(g_httpd, &test);
    }
}

static void controls_mute_cb(bool muted, void *user)
{
    g_muted = muted;
    if (muted || g_privacy) {
        ledring_set_state(LEDRING_STATE_MUTED);
    } else if (ws_audio_is_connected()) {
        ledring_set_state(LEDRING_STATE_ONLINE_IDLE);
    }
    nvs_save_config(&g_config);
}

static void controls_mode_cb(const char *mode, void *user)
{
    strlcpy(g_config.mode, mode, sizeof(g_config.mode));
    g_ptt_mode = strcmp(mode, "ptt") == 0;
    ws_audio_notify_mode(mode);
    nvs_save_config(&g_config);
}

static void controls_volume_cb(int volume, void *user)
{
    g_config.vol_spk = volume;
    float gain = (float)volume / 100.0f;
    audio_set_spk_gain(gain);
    ledring_set_volume(volume);
    nvs_save_config(&g_config);
}

static void controls_test_cb(void *user)
{
    play_test_beep();
}

static void controls_privacy_cb(bool privacy, void *user)
{
    g_privacy = privacy;
    if (privacy) {
        ledring_set_state(LEDRING_STATE_MUTED);
    } else if (g_muted) {
        ledring_set_state(LEDRING_STATE_MUTED);
    } else if (ws_audio_is_connected()) {
        ledring_set_state(LEDRING_STATE_ONLINE_IDLE);
    }
}

static void play_test_beep(void)
{
    const int sample_rate = 16000;
    const int duration_ms = 150;
    const int samples = sample_rate * duration_ms / 1000;
    int16_t *buf = malloc(samples * sizeof(int16_t));
    if (!buf) {
        return;
    }
    for (int tone = 0; tone < 2; ++tone) {
        float freq = tone == 0 ? 440.0f : 660.0f;
        for (int i = 0; i < samples; ++i) {
            float t = (float)i / sample_rate;
            float s = sinf(2.0f * 3.14159f * freq * t);
            buf[i] = (int16_t)(s * 20000);
        }
        audio_write(buf, samples, pdMS_TO_TICKS(duration_ms));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    free(buf);
}

static void audio_capture_task(void *arg)
{
    int16_t frame[FRAME_SAMPLES];
    while (1) {
        size_t got = audio_read(frame, FRAME_SAMPLES, portMAX_DELAY);
        if (got == FRAME_SAMPLES) {
            int64_t sum = 0;
            for (size_t i = 0; i < got; ++i) {
                sum += llabs(frame[i]);
            }
            float vu = (float)sum / (got * 32768.0f);
            ledring_set_vu(vu);
            if (!g_muted && !g_privacy && ws_audio_is_connected() &&
                (!g_ptt_mode || controls_button_is_pressed())) {
                ws_audio_queue_tx((const uint8_t *)frame, FRAME_SAMPLES * sizeof(int16_t));
                ledring_set_state(LEDRING_STATE_STREAMING_TX);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void audio_play_task(void *arg)
{
    QueueHandle_t rx_queue = ws_audio_get_rx_queue();
    ws_audio_pcm_frame_t frame;
    while (1) {
        if (xQueueReceive(rx_queue, &frame, portMAX_DELAY) == pdTRUE) {
            if (!g_privacy) {
                audio_write((const int16_t *)frame.data, frame.len / sizeof(int16_t), portMAX_DELAY);
                ledring_set_state(LEDRING_STATE_STREAMING_RX);
            }
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    g_wifi_sta = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ledring_init(board_get_pins()->led_data, board_get_pins()->led_power, CONFIG_PUCK_LED_COUNT);
    ledring_start();
    ledring_set_state(LEDRING_STATE_BOOT);

    memset(&g_config, 0, sizeof(g_config));
    strlcpy(g_config.mode, "always_on", sizeof(g_config.mode));
    g_config.vol_spk = 80;
    g_config.vol_mic = 100;

    if (nvs_load_config(&g_config) != ESP_OK || strlen(g_config.ssid) == 0) {
        portal_config_t defaults = { 0 };
        strlcpy(defaults.server_url, "ws://aiboss.lan.home.malaiwah.com:7000/", sizeof(defaults.server_url));
        strlcpy(defaults.mode, "always_on", sizeof(defaults.mode));
        portal_result_t result = { 0 };
        ESP_LOGI(TAG, "Launching provisioning portal");
        portal_run(&defaults, &result);
        strlcpy(g_config.ssid, result.ssid, sizeof(g_config.ssid));
        strlcpy(g_config.pass, result.password, sizeof(g_config.pass));
        strlcpy(g_config.server_url, result.server_url, sizeof(g_config.server_url));
        strlcpy(g_config.token, result.token, sizeof(g_config.token));
        strlcpy(g_config.mode, result.mode, sizeof(g_config.mode));
        g_config.vol_spk = 80;
        g_config.vol_mic = 100;
        nvs_save_config(&g_config);
        portal_stop();
        ESP_LOGI(TAG, "Provisioning complete. Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    controls_pins_t c_pins = {
        .button_k0 = board_get_pins()->button_k0,
        .mic_switch = board_get_pins()->mic_switch,
        .encoder_a = board_get_pins()->encoder_a,
        .encoder_b = board_get_pins()->encoder_b,
        .encoder_sw = board_get_pins()->encoder_sw,
    };
    controls_callbacks_t callbacks = {
        .mute_cb = controls_mute_cb,
        .mode_cb = controls_mode_cb,
        .volume_cb = controls_volume_cb,
        .test_cb = controls_test_cb,
        .privacy_cb = controls_privacy_cb,
        .user_ctx = NULL,
    };
    controls_init(&c_pins, &callbacks);
    controls_set_volume(g_config.vol_spk);
    controls_set_mode(g_config.mode);
    g_muted = false;
    g_privacy = false;
    g_ptt_mode = strcmp(g_config.mode, "ptt") == 0;

    audio_pins_t audio_pins = {
        .mic_bclk = board_get_pins()->mic_bclk,
        .mic_ws = board_get_pins()->mic_ws,
        .mic_data_in = board_get_pins()->mic_data_in,
        .spk_bclk = board_get_pins()->spk_bclk,
        .spk_ws = board_get_pins()->spk_ws,
        .spk_data_out = board_get_pins()->spk_data_out,
    };
    audio_init(&audio_pins);
    audio_set_spk_gain((float)g_config.vol_spk / 100.0f);
    audio_set_mic_gain((float)g_config.vol_mic / 100.0f);

    ws_audio_init(FRAME_SAMPLES * sizeof(int16_t));

    g_wifi_events = xEventGroupCreate();
    esp_event_handler_instance_t wifi_any_id;
    esp_event_handler_instance_t got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &wifi_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &got_ip);

    start_wifi_sta(&g_config);

    xTaskCreate(audio_capture_task, "audio_cap", 4096, NULL, 6, NULL);
    xTaskCreate(audio_play_task, "audio_play", 4096, NULL, 6, NULL);

    start_http_server();

    EventBits_t bits = xEventGroupWaitBits(g_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi connected. Starting WebSocket client");
        ws_audio_start(g_config.server_url, g_config.token);
    }

    while (1) {
        if (ws_audio_is_connected() && !g_muted && !g_privacy) {
            ledring_set_state(LEDRING_STATE_ONLINE_IDLE);
        } else if (!ws_audio_is_connected()) {
            ledring_set_state(LEDRING_STATE_WIFI_CONNECTING);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

