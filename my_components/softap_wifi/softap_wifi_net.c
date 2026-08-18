#include "softap_wifi_net.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lwip/ip4_addr.h"

#define TAG "softap_wifi_net"

#define SOFTAP_SSID              "ESP32"
#define SOFTAP_PASSWORD          "12345678"
#define SOFTAP_CHANNEL           6
#define SOFTAP_MAX_CONNECTIONS   4
#define SOFTAP_SCAN_MAX_RESULTS  10U
#define SOFTAP_STA_MAX_RETRIES   5U

static SemaphoreHandle_t s_scan_lock;
static TaskHandle_t s_scan_task;
static softap_wifi_scan_done_cb_t s_scan_callback;
static void *s_scan_callback_ctx;
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static bool s_initialized;
static bool s_connect_requested;
static unsigned s_retry_count;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "配网设备已连接 SoftAP");
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "配网设备已断开 SoftAP");
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        ESP_LOGW(TAG, "STA 已断开，原因码=%d", event->reason);

        if (s_connect_requested && s_retry_count < SOFTAP_STA_MAX_RETRIES) {
            ++s_retry_count;
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "STA 重连请求失败：%s", esp_err_to_name(err));
            }
        } else if (s_connect_requested) {
            ESP_LOGE(TAG, "STA 重试 %u 次后仍未连接，已停止重试", s_retry_count);
        }
        return;
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        s_retry_count = 0;
        ESP_LOGI(TAG, "STA 连接成功，IP=" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t softap_wifi_net_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL,
        &s_wifi_event_instance);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        wifi_event_handler,
        NULL,
        &s_ip_event_instance);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t ap_config = {
        .ap = {
            .ssid = SOFTAP_SSID,
            .password = SOFTAP_PASSWORD,
            .ssid_len = sizeof(SOFTAP_SSID) - 1U,
            .channel = SOFTAP_CHANNEL,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = SOFTAP_MAX_CONNECTIONS,
        },
    };

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        return err;
    }

    esp_netif_ip_info_t ip_info = {0};
    IP4_ADDR(&ip_info.ip, 192, 168, 100, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 100, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    (void)esp_netif_dhcps_stop(s_ap_netif);
    err = esp_netif_set_ip_info(s_ap_netif, &ip_info);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_netif_dhcps_start(s_ap_netif);
    if (err != ESP_OK) {
        return err;
    }

    s_scan_lock = xSemaphoreCreateBinary();
    if (s_scan_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_scan_lock);

    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "SoftAP 已启动：SSID=%s，IP=192.168.100.1", SOFTAP_SSID);
    return ESP_OK;
}

esp_err_t softap_wifi_net_connect(const char *ssid, const char *password)
{
    if (!s_initialized || ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t ssid_len = strlen(ssid);
    const size_t password_len = strlen(password);
    if (ssid_len == 0U || ssid_len > sizeof(((wifi_config_t *)0)->sta.ssid) ||
        password_len < 8U || password_len >= sizeof(((wifi_config_t *)0)->sta.password)) {
        return ESP_ERR_INVALID_SIZE;
    }

    wifi_config_t sta_config = {0};
    memcpy(sta_config.sta.ssid, ssid, ssid_len);
    memcpy(sta_config.sta.password, password, password_len);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    s_connect_requested = true;
    s_retry_count = 0;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        return err;
    }
    return esp_wifi_connect();
}

static void scan_task(void *arg)
{
    (void)arg;
    wifi_ap_record_t records[SOFTAP_SCAN_MAX_RESULTS] = {0};
    uint16_t count = SOFTAP_SCAN_MAX_RESULTS;
    esp_err_t result = esp_wifi_scan_start(NULL, true);

    if (result == ESP_OK) {
        result = esp_wifi_scan_get_ap_records(&count, records);
    }

    softap_wifi_scan_done_cb_t callback = s_scan_callback;
    void *callback_ctx = s_scan_callback_ctx;
    s_scan_callback = NULL;
    s_scan_callback_ctx = NULL;
    s_scan_task = NULL;
    xSemaphoreGive(s_scan_lock);

    if (callback != NULL) {
        callback(records, (result == ESP_OK) ? count : 0U, result, callback_ctx);
    }
    vTaskDelete(NULL);
}

esp_err_t softap_wifi_net_scan_start(softap_wifi_scan_done_cb_t callback, void *ctx)
{
    if (!s_initialized || callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_scan_lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    s_scan_callback = callback;
    s_scan_callback_ctx = ctx;
    if (xTaskCreate(scan_task, "softap_scan", 4096, NULL, 3, &s_scan_task) != pdPASS) {
        s_scan_callback = NULL;
        s_scan_callback_ctx = NULL;
        xSemaphoreGive(s_scan_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t softap_wifi_net_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    s_connect_requested = false;
    if (s_scan_task != NULL) {
        vTaskDelete(s_scan_task);
        s_scan_task = NULL;
    }

    esp_err_t result = esp_wifi_stop();
    (void)esp_event_handler_instance_unregister(
        WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_instance);
    (void)esp_event_handler_instance_unregister(
        IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_event_instance);
    (void)esp_wifi_deinit();

    if (s_ap_netif != NULL) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    if (s_sta_netif != NULL) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_scan_lock != NULL) {
        vSemaphoreDelete(s_scan_lock);
        s_scan_lock = NULL;
    }

    s_initialized = false;
    return result;
}
