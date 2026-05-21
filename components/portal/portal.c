#include "portal.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_check.h"

#define TAG "portal"

#define PORTAL_SUBMIT_BIT BIT0

static httpd_handle_t s_httpd = NULL;
static EventGroupHandle_t s_events;
static portal_result_t s_result;

static const char *portal_form_html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Puck Setup</title>"
    "<style>body{font-family:sans-serif;margin:2em;background:#111;color:#eee;}"
    "label{display:block;margin-top:1em;}input,select{width:100%;padding:0.4em;}"
    "button{margin-top:1.5em;padding:0.6em 1em;}div.card{max-width:420px;margin:auto;background:#1e1e1e;padding:1.5em;border-radius:12px;box-shadow:0 4px 20px rgba(0,0,0,0.4);}"
    "</style></head><body><div class='card'><h2>Puck provisioning</h2>"
    "<form method='POST' action='/save'>"
    "<label>Wi-Fi SSID<input name='ssid' maxlength='32' required></label>"
    "<label>Wi-Fi Password<input name='pass' maxlength='64' type='password'></label>"
    "<label>Server URL<input name='server' maxlength='120' value='ws://aiboss.lan.home.malaiwah.com:7000/'></label>"
    "<label>Token<input name='token' maxlength='120'></label>"
    "<label>Mode<select name='mode'><option value='always_on'>Always on</option><option value='ptt'>Push to talk</option></select></label>"
    "<input type='hidden' name='sr_in' value='16000'><input type='hidden' name='sr_out' value='16000'>"
    "<button type='submit'>Save & reboot</button></form></div></body></html>";

static esp_err_t portal_root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, portal_form_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            a = a <= '9' ? a - '0' : (tolower(a) - 'a' + 10);
            b = b <= '9' ? b - '0' : (tolower(b) - 'a' + 10);
            *dst++ = (char)(16 * a + b);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void parse_form(const char *body)
{
    char *buf = strdup(body);
    if (!buf) {
        return;
    }
    char *token = strtok(buf, "&");
    while (token) {
        char *eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            char value[128];
            url_decode(value, eq + 1);
            if (strcmp(token, "ssid") == 0) {
                strlcpy(s_result.ssid, value, sizeof(s_result.ssid));
            } else if (strcmp(token, "pass") == 0) {
                strlcpy(s_result.password, value, sizeof(s_result.password));
            } else if (strcmp(token, "server") == 0) {
                strlcpy(s_result.server_url, value, sizeof(s_result.server_url));
            } else if (strcmp(token, "token") == 0) {
                strlcpy(s_result.token, value, sizeof(s_result.token));
            } else if (strcmp(token, "mode") == 0) {
                strlcpy(s_result.mode, value, sizeof(s_result.mode));
            } else if (strcmp(token, "sr_in") == 0) {
                s_result.sample_rate_in = atoi(value);
            } else if (strcmp(token, "sr_out") == 0) {
                s_result.sample_rate_out = atoi(value);
            }
        }
        token = strtok(NULL, "&");
    }
    free(buf);
}

static esp_err_t portal_save_handler(httpd_req_t *req)
{
    size_t len = req->content_len;
    char *buf = malloc(len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    int received = httpd_req_recv(req, buf, len);
    if (received <= 0) {
        free(buf);
        return ESP_FAIL;
    }
    buf[received] = '\0';
    parse_form(buf);
    free(buf);

    const char *resp = "<html><body><h2>Saved! Rebooting...</h2></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    xEventGroupSetBits(s_events, PORTAL_SUBMIT_BIT);
    return ESP_OK;
}

static httpd_handle_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = portal_root_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t save = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = portal_save_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server, &save);
    }
    return server;
}

static void stop_http_server(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
    }
}

static esp_err_t start_softap(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_stop();
    esp_wifi_deinit();

    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "wifi mode");
    wifi_config_t ap_cfg = { 0 };
    strlcpy((char *)ap_cfg.ap.ssid, "Puck-Setup", sizeof(ap_cfg.ap.ssid));
    strlcpy((char *)ap_cfg.ap.password, "voice-setup", sizeof(ap_cfg.ap.password));
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    if (strlen("voice-setup") == 0) {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), TAG, "wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    ESP_LOGI(TAG, "SoftAP started: SSID=%s", ap_cfg.ap.ssid);
    return ESP_OK;
}

static void stop_softap(void)
{
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_NULL);
}

esp_err_t portal_run(portal_config_t *defaults, portal_result_t *out_result)
{
    ESP_LOGI(TAG, "Starting provisioning portal");
    memset(&s_result, 0, sizeof(s_result));
    if (defaults) {
        strlcpy(s_result.server_url, defaults->server_url, sizeof(s_result.server_url));
        strlcpy(s_result.mode, defaults->mode, sizeof(s_result.mode));
    }
    s_result.sample_rate_in = 16000;
    s_result.sample_rate_out = 16000;

    if (!s_events) {
        s_events = xEventGroupCreate();
    }
    xEventGroupClearBits(s_events, PORTAL_SUBMIT_BIT);

    ESP_ERROR_CHECK(start_softap());
    s_httpd = start_http_server();
    if (!s_httpd) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    EventBits_t bits = xEventGroupWaitBits(s_events, PORTAL_SUBMIT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    if (!(bits & PORTAL_SUBMIT_BIT)) {
        ESP_LOGW(TAG, "Portal wait cancelled");
        return ESP_FAIL;
    }

    if (out_result) {
        *out_result = s_result;
    }
    ESP_LOGI(TAG, "Provisioning data captured");
    return ESP_OK;
}

void portal_stop(void)
{
    stop_http_server(s_httpd);
    s_httpd = NULL;
    stop_softap();
    if (s_events) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }
}

