#include "ws_audio.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_mac.h"
#include "esp_check.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "ws_audio"

#define WS_AUDIO_MAX_FRAME_BYTES 960
#define WS_AUDIO_HEADER_BYTES sizeof(ws_frame_header_t)

static QueueHandle_t s_tx_queue;
static QueueHandle_t s_rx_queue;
static esp_websocket_client_handle_t s_client;
static bool s_connected;
static char s_token[128];
static size_t s_frame_bytes;
static TaskHandle_t s_tx_task;
static uint16_t s_seq;
static char s_mode[16] = "always_on";

typedef struct {
    size_t len;
    uint8_t data[WS_AUDIO_HEADER_BYTES + WS_AUDIO_MAX_FRAME_BYTES];
} ws_tx_item_t;

static void ws_audio_tx_task(void *arg);

static void send_hello(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char device_id[18];
    snprintf(device_id, sizeof(device_id), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    cJSON *hello = cJSON_CreateObject();
    cJSON_AddStringToObject(hello, "type", "hello");
    cJSON_AddStringToObject(hello, "device_id", device_id);
    cJSON_AddStringToObject(hello, "token", s_token);
    cJSON *cap = cJSON_CreateObject();
    cJSON_AddNumberToObject(cap, "sr", 16000);
    cJSON_AddNumberToObject(cap, "ch", 1);
    cJSON_AddStringToObject(cap, "fmt", "s16le");
    cJSON_AddItemToObject(hello, "cap", cap);
    cJSON_AddStringToObject(hello, "mode", s_mode);
    char *payload = cJSON_PrintUnformatted(hello);
    if (payload) {
        esp_websocket_client_send_text(s_client, payload, strlen(payload), portMAX_DELAY);
        cJSON_free(payload);
    }
    cJSON_Delete(hello);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        s_connected = true;
        s_seq = 0;
        send_hello();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected");
        s_connected = false;
        break;
    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == WS_TRANSPORT_OPCODES_BINARY && data->data_len >= WS_AUDIO_HEADER_BYTES) {
            const ws_frame_header_t *hdr = (const ws_frame_header_t *)data->data_ptr;
            if (hdr->ver == 1 && hdr->kind == 1) {
                size_t payload = data->data_len - WS_AUDIO_HEADER_BYTES;
                if (payload > sizeof(((ws_audio_pcm_frame_t *)0)->data)) {
                    payload = sizeof(((ws_audio_pcm_frame_t *)0)->data);
                }
                ws_audio_pcm_frame_t item = { 0 };
                item.len = payload;
                memcpy(item.data, data->data_ptr + WS_AUDIO_HEADER_BYTES, payload);
                if (s_rx_queue) {
                    xQueueSend(s_rx_queue, &item, 0);
                }
            }
        } else if (data->op_code == WS_TRANSPORT_OPCODES_TEXT) {
            ESP_LOGI(TAG, "WS text: %.*s", data->data_len, (const char *)data->data_ptr);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        s_connected = false;
        break;
    default:
        break;
    }
}

esp_err_t ws_audio_init(size_t frame_bytes)
{
    s_frame_bytes = frame_bytes;
    if (!s_tx_queue) {
        s_tx_queue = xQueueCreate(16, sizeof(ws_tx_item_t));
    }
    if (!s_rx_queue) {
        s_rx_queue = xQueueCreate(16, sizeof(ws_audio_pcm_frame_t));
    }
    return s_tx_queue && s_rx_queue ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t ws_audio_start(const char *url, const char *token)
{
    if (!url) {
        return ESP_ERR_INVALID_ARG;
    }
    if (token) {
        strlcpy(s_token, token, sizeof(s_token));
    } else {
        s_token[0] = '\0';
    }

    esp_websocket_client_config_t cfg = {
        .uri = url,
        .disable_auto_reconnect = false,
        .reconnect_timeout_ms = 5000,
    };

    s_client = esp_websocket_client_init(&cfg);
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_ERR_NO_MEM, TAG, "client alloc");

    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);

    esp_err_t err = esp_websocket_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket: %s", esp_err_to_name(err));
        return err;
    }

    if (!s_tx_task) {
        xTaskCreate(ws_audio_tx_task, "ws_tx", 4096, NULL, 5, &s_tx_task);
    }
    return ESP_OK;
}

void ws_audio_stop(void)
{
    if (s_tx_task) {
        vTaskDelete(s_tx_task);
        s_tx_task = NULL;
    }
    if (s_client) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
}

static void ws_audio_tx_task(void *arg)
{
    ws_tx_item_t item;
    while (1) {
        if (xQueueReceive(s_tx_queue, &item, portMAX_DELAY) == pdTRUE) {
            if (s_connected && s_client) {
                esp_err_t err = esp_websocket_client_send_bin(s_client, (const char *)item.data, item.len, portMAX_DELAY);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "send_bin failed: %s", esp_err_to_name(err));
                }
            }
        }
    }
}

bool ws_audio_is_connected(void)
{
    return s_connected;
}

QueueHandle_t ws_audio_get_rx_queue(void)
{
    return s_rx_queue;
}

QueueHandle_t ws_audio_get_tx_queue(void)
{
    return s_tx_queue;
}

esp_err_t ws_audio_queue_tx(const uint8_t *pcm, size_t len)
{
    if (!s_tx_queue || !pcm || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > WS_AUDIO_MAX_FRAME_BYTES) {
        len = WS_AUDIO_MAX_FRAME_BYTES;
    }
    ws_tx_item_t item = { 0 };
    ws_frame_header_t hdr = {
        .ver = 1,
        .kind = 0,
        .seq = s_seq++,
        .nbytes = len,
    };
    memcpy(item.data, &hdr, sizeof(hdr));
    memcpy(item.data + sizeof(hdr), pcm, len);
    item.len = sizeof(hdr) + len;
    if (xQueueSend(s_tx_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "TX queue full, dropping frame");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void ws_audio_notify_mode(const char *mode)
{
    if (mode) {
        strlcpy(s_mode, mode, sizeof(s_mode));
        if (s_connected) {
            send_hello();
        }
    }
}

