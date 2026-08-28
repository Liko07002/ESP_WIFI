#include "softap_wifi_net.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"

#include "softap_wifi_config.h"
#include "softap_wifi_store.h"

#define TAG "softap_wifi_net"

_Static_assert(sizeof(SOFTAP_WIFI_AP_SSID) > 1U &&
               sizeof(SOFTAP_WIFI_AP_SSID) <= 33U,
               "配网热点名称长度必须为 1～32 字节");
_Static_assert(sizeof(SOFTAP_WIFI_AP_PASSWORD) <= 64U,
               "配网热点密码不能超过 63 字节");
_Static_assert(SOFTAP_WIFI_AP_CHANNEL >= 1 && SOFTAP_WIFI_AP_CHANNEL <= 13,
               "配网热点信道必须为 1～13");
_Static_assert(SOFTAP_WIFI_AP_MAX_CONNECTIONS >= 1 &&
               SOFTAP_WIFI_AP_MAX_CONNECTIONS <= 10,
               "配网热点最大连接数必须为 1～10");

static SemaphoreHandle_t s_scan_lock;
static SemaphoreHandle_t s_ap_start_signal;
static TaskHandle_t s_scan_task;
static softap_wifi_scan_done_cb_t s_scan_callback;
static void *s_scan_callback_ctx;
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_event_instance;
static esp_event_handler_instance_t s_ip_event_instance;
static softap_wifi_net_status_cb_t s_status_callback;
static void *s_status_callback_ctx;
static bool s_netif_initialized_here;
static bool s_event_loop_created_here;
static bool s_wifi_initialized;
static bool s_wifi_handler_registered;
static bool s_ip_handler_registered;
static bool s_wifi_started;
static bool s_initialized;
static bool s_ap_started;
static char s_captive_portal_uri[40];

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "配网终端已连接热点");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        if (s_ap_start_signal != NULL) {
            xSemaphoreGive(s_ap_start_signal);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "配网终端已断开热点");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        ESP_LOGW(TAG, "STA 已断开，原因码=%u", (unsigned)event->reason);
        if (s_status_callback != NULL) {
            s_status_callback(false, NULL, event->reason, s_status_callback_ctx);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "STA 已取得 IP：%s", ip);
        if (s_status_callback != NULL) {
            s_status_callback(true, ip, 0, s_status_callback_ctx);
        }
    }
}

static void make_ap_ssid(uint8_t *ssid, uint8_t *ssid_len)
{
    char value[33];
#if SOFTAP_WIFI_AP_APPEND_MAC_SUFFIX
    _Static_assert(sizeof(SOFTAP_WIFI_AP_SSID) <= 28U,
                   "追加 MAC 后缀时热点名称最多为 27 字节");
    uint8_t mac[6] = {0};
    (void)esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(value, sizeof(value), "%s-%02X%02X", SOFTAP_WIFI_AP_SSID, mac[4], mac[5]);
#else
    snprintf(value, sizeof(value), "%s", SOFTAP_WIFI_AP_SSID);
#endif
    *ssid_len = (uint8_t)strnlen(value, sizeof(value) - 1U);
    memcpy(ssid, value, *ssid_len);
}

static esp_err_t configure_ap(void)
{
    wifi_config_t config = {0};
    make_ap_ssid(config.ap.ssid, &config.ap.ssid_len);
    size_t password_len = sizeof(SOFTAP_WIFI_AP_PASSWORD) - 1U;
    if (password_len != 0U && (password_len < 8U || password_len > 63U)) {
        ESP_LOGE(TAG, "配网热点密码必须为空或长度为 8～63 字节");
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(config.ap.password, SOFTAP_WIFI_AP_PASSWORD, password_len);
    config.ap.channel = SOFTAP_WIFI_AP_CHANNEL;
    config.ap.max_connection = SOFTAP_WIFI_AP_MAX_CONNECTIONS;
    config.ap.authmode = password_len == 0U ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err != ESP_OK) {
        return err;
    }
    esp_netif_ip_info_t ip_info = {0};
    IP4_ADDR(&ip_info.ip, SOFTAP_WIFI_AP_IP_A, SOFTAP_WIFI_AP_IP_B,
             SOFTAP_WIFI_AP_IP_C, SOFTAP_WIFI_AP_IP_D);
    ip_info.gw = ip_info.ip;
    IP4_ADDR(&ip_info.netmask, SOFTAP_WIFI_AP_NETMASK_A,
             SOFTAP_WIFI_AP_NETMASK_B, SOFTAP_WIFI_AP_NETMASK_C,
             SOFTAP_WIFI_AP_NETMASK_D);
    (void)esp_netif_dhcps_stop(s_ap_netif);
    err = esp_netif_set_ip_info(s_ap_netif, &ip_info);
    if (err != ESP_OK) {
        return err;
    }
    snprintf(s_captive_portal_uri, sizeof(s_captive_portal_uri),
             "http://%u.%u.%u.%u/", SOFTAP_WIFI_AP_IP_A,
             SOFTAP_WIFI_AP_IP_B, SOFTAP_WIFI_AP_IP_C, SOFTAP_WIFI_AP_IP_D);
    err = esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET,
                                 ESP_NETIF_CAPTIVEPORTAL_URI,
                                 s_captive_portal_uri,
                                 strlen(s_captive_portal_uri));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "设置配网页面地址通知失败：%s", esp_err_to_name(err));
    }
    return esp_netif_dhcps_start(s_ap_netif);
}

esp_err_t softap_wifi_net_init(softap_wifi_net_status_cb_t callback, void *ctx)
{
    if (s_initialized) return ESP_ERR_INVALID_STATE;
    if (callback == NULL) return ESP_ERR_INVALID_ARG;
    s_status_callback = callback;
    s_status_callback_ctx = ctx;

    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) s_netif_initialized_here = true;
    else if (err != ESP_ERR_INVALID_STATE) goto fail;
    err = esp_event_loop_create_default();
    if (err == ESP_OK) s_event_loop_created_here = true;
    else if (err != ESP_ERR_INVALID_STATE) goto fail;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init_config)) != ESP_OK) goto fail;
    s_wifi_initialized = true;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) goto fail;
    if ((err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
            wifi_event_handler, NULL, &s_wifi_event_instance)) != ESP_OK) goto fail;
    s_wifi_handler_registered = true;
    if ((err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
            wifi_event_handler, NULL, &s_ip_event_instance)) != ESP_OK) goto fail;
    s_ip_handler_registered = true;
    s_scan_lock = xSemaphoreCreateBinary();
    s_ap_start_signal = xSemaphoreCreateBinary();
    if (s_scan_lock == NULL || s_ap_start_signal == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    xSemaphoreGive(s_scan_lock);
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) goto fail;
    if ((err = esp_wifi_start()) != ESP_OK) goto fail;
    s_wifi_started = true;
    s_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi 网络层已初始化");
    return ESP_OK;
fail:
    (void)softap_wifi_net_deinit();
    return err;
}

esp_err_t softap_wifi_net_connect(const char *ssid, const char *password)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (ssid == NULL || password == NULL) return ESP_ERR_INVALID_ARG;
    size_t ssid_len = strnlen(ssid, SOFTAP_WIFI_SSID_MAX_LEN + 1U);
    size_t password_len = strnlen(password, SOFTAP_WIFI_PASSWORD_MAX_LEN + 1U);
    if (ssid_len == 0U || ssid_len > SOFTAP_WIFI_SSID_MAX_LEN ||
        password_len > SOFTAP_WIFI_PASSWORD_MAX_LEN ||
        (password_len > 0U && password_len < 8U)) return ESP_ERR_INVALID_SIZE;

    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, ssid_len);
    memcpy(config.sta.password, password, password_len);
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    (void)esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "开始连接 Wi-Fi：%s", ssid);
        err = esp_wifi_connect();
    }
    return err;
}

esp_err_t softap_wifi_net_reconnect(void)
{
    return s_initialized ? esp_wifi_connect() : ESP_ERR_INVALID_STATE;
}

esp_err_t softap_wifi_net_disconnect(void)
{
    return s_initialized ? esp_wifi_disconnect() : ESP_ERR_INVALID_STATE;
}

esp_err_t softap_wifi_net_ap_start(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_ap_started) return ESP_OK;
    (void)xSemaphoreTake(s_ap_start_signal, 0);
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) {
        if (xSemaphoreTake(s_ap_start_signal, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "等待配网热点启动超时");
            err = ESP_ERR_TIMEOUT;
        } else {
            err = configure_ap();
        }
    }
    if (err == ESP_OK) {
        s_ap_started = true;
        ESP_LOGI(TAG, "配网热点已启动，地址=%u.%u.%u.%u",
                 SOFTAP_WIFI_AP_IP_A, SOFTAP_WIFI_AP_IP_B,
                 SOFTAP_WIFI_AP_IP_C, SOFTAP_WIFI_AP_IP_D);
    } else {
        (void)esp_wifi_set_mode(WIFI_MODE_STA);
    }
    return err;
}

esp_err_t softap_wifi_net_ap_stop(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!s_ap_started) return ESP_OK;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        s_ap_started = false;
        ESP_LOGI(TAG, "配网热点已关闭");
    }
    return err;
}

static void scan_task(void *arg)
{
    (void)arg;
    wifi_ap_record_t records[SOFTAP_WIFI_SCAN_MAX_RESULTS] = {0};
    uint16_t count = SOFTAP_WIFI_SCAN_MAX_RESULTS;
    esp_err_t result = esp_wifi_scan_start(NULL, true);
    if (result == ESP_OK) result = esp_wifi_scan_get_ap_records(&count, records);
    softap_wifi_scan_done_cb_t callback = s_scan_callback;
    void *callback_ctx = s_scan_callback_ctx;
    s_scan_callback = NULL;
    s_scan_callback_ctx = NULL;
    s_scan_task = NULL;
    xSemaphoreGive(s_scan_lock);
    if (callback != NULL) callback(records, result == ESP_OK ? count : 0U, result, callback_ctx);
    vTaskDelete(NULL);
}

esp_err_t softap_wifi_net_scan_start(softap_wifi_scan_done_cb_t callback, void *ctx)
{
    if (!s_initialized || callback == NULL) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_scan_lock, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
    s_scan_callback = callback;
    s_scan_callback_ctx = ctx;
    if (xTaskCreatePinnedToCore(scan_task, "softap_scan", SOFTAP_WIFI_SCAN_TASK_STACK, NULL,
                    SOFTAP_WIFI_SCAN_TASK_PRIORITY, &s_scan_task, SOFTAP_WIFI_SCAN_TASK_CORE) != pdPASS) {
        s_scan_callback = NULL;
        s_scan_callback_ctx = NULL;
        xSemaphoreGive(s_scan_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t softap_wifi_net_deinit(void)
{
    if (s_scan_task != NULL) {
        vTaskDelete(s_scan_task);
        s_scan_task = NULL;
    }
    if (s_wifi_started) { (void)esp_wifi_stop(); s_wifi_started = false; }
    if (s_ip_handler_registered) {
        (void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_event_instance);
        s_ip_handler_registered = false;
    }
    if (s_wifi_handler_registered) {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_event_instance);
        s_wifi_handler_registered = false;
    }
    if (s_wifi_initialized) { (void)esp_wifi_deinit(); s_wifi_initialized = false; }
    if (s_ap_netif != NULL) { esp_netif_destroy_default_wifi(s_ap_netif); s_ap_netif = NULL; }
    if (s_sta_netif != NULL) { esp_netif_destroy_default_wifi(s_sta_netif); s_sta_netif = NULL; }
    if (s_scan_lock != NULL) { vSemaphoreDelete(s_scan_lock); s_scan_lock = NULL; }
    if (s_ap_start_signal != NULL) {
        vSemaphoreDelete(s_ap_start_signal);
        s_ap_start_signal = NULL;
    }
    if (s_event_loop_created_here) { (void)esp_event_loop_delete_default(); s_event_loop_created_here = false; }
    if (s_netif_initialized_here) { (void)esp_netif_deinit(); s_netif_initialized_here = false; }
    s_status_callback = NULL;
    s_status_callback_ctx = NULL;
    s_initialized = false;
    s_ap_started = false;
    return ESP_OK;
}
