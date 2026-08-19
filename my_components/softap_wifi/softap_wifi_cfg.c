#include "softap_wifi_cfg.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "softap_wifi_http.h"
#include "softap_wifi_net.h"

#define TAG "softap_wifi_cfg"

#define SOFTAP_WIFI_SSID_MAX_LEN      32U
#define SOFTAP_WIFI_PASSWORD_MIN_LEN   8U
#define SOFTAP_WIFI_PASSWORD_MAX_LEN  63U

#define EVENT_CONNECT_REQUEST  BIT0
#define EVENT_CONNECT_SUCCESS  BIT1
#define EVENT_CONNECT_FAILED   BIT2
#define EVENT_STATE_REQUEST    BIT3
#define EVENT_CLOSE_REQUEST    BIT4
#define EVENT_STA_HAS_IP       BIT5
#define EVENT_ALL              (EVENT_CONNECT_REQUEST | EVENT_CONNECT_SUCCESS | \
                                EVENT_CONNECT_FAILED | EVENT_STATE_REQUEST | \
                                EVENT_CLOSE_REQUEST)

typedef enum {
    PROVISION_STATE_READY,
    PROVISION_STATE_CONNECTING,
    PROVISION_STATE_CONNECTED,
    PROVISION_STATE_FAILED,
    PROVISION_STATE_CLOSING,
} provision_state_t;

typedef struct {
    char ssid[SOFTAP_WIFI_SSID_MAX_LEN + 1U];
    char password[SOFTAP_WIFI_PASSWORD_MAX_LEN + 1U];
} softap_wifi_credentials_t;

static EventGroupHandle_t s_event_group;
static TaskHandle_t s_manager_task;
static softap_wifi_credentials_t s_credentials;
static provision_state_t s_state = PROVISION_STATE_READY;
static char s_sta_ip[16];
static int s_failure_reason;
static bool s_started;

static esp_err_t credentials_store(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_len = strnlen(ssid, SOFTAP_WIFI_SSID_MAX_LEN + 1U);
    const size_t password_len = strnlen(password, SOFTAP_WIFI_PASSWORD_MAX_LEN + 1U);
    if (ssid_len == 0U || ssid_len > SOFTAP_WIFI_SSID_MAX_LEN ||
        password_len < SOFTAP_WIFI_PASSWORD_MIN_LEN ||
        password_len > SOFTAP_WIFI_PASSWORD_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(s_credentials.ssid, ssid, ssid_len);
    s_credentials.ssid[ssid_len] = '\0';
    memcpy(s_credentials.password, password, password_len);
    s_credentials.password[password_len] = '\0';
    return ESP_OK;
}

static esp_err_t send_current_state(void)
{
    switch (s_state) {
        case PROVISION_STATE_CONNECTING:
            return softap_wifi_http_send_status(
                "connecting", "正在连接路由器，请稍候…", NULL, 0);
        case PROVISION_STATE_CONNECTED:
            return softap_wifi_http_send_status(
                "connected", "设备已成功连接路由器", s_sta_ip, 0);
        case PROVISION_STATE_FAILED:
            return softap_wifi_http_send_status(
                "failed", "连接失败，请检查密码或信号后重试", NULL, s_failure_reason);
        case PROVISION_STATE_CLOSING:
            return softap_wifi_http_send_status(
                "closing", "配网已完成，热点即将关闭", s_sta_ip, 0);
        case PROVISION_STATE_READY:
        default:
            return softap_wifi_http_send_status(
                "ready", "请选择需要连接的 Wi-Fi", NULL, 0);
    }
}

static void scan_done(
    const wifi_ap_record_t *records,
    size_t count,
    esp_err_t result,
    void *ctx)
{
    (void)ctx;
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi 扫描失败：%s", esp_err_to_name(result));
        (void)softap_wifi_http_send_status(
            "scan_failed", "扫描失败，请稍后重试", NULL, result);
        return;
    }

    esp_err_t err = softap_wifi_http_send_scan_result(records, count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "发送扫描结果失败：%s", esp_err_to_name(err));
    }
}

static esp_err_t scan_requested(void *ctx)
{
    (void)ctx;
    if (s_state == PROVISION_STATE_CONNECTING ||
        s_state == PROVISION_STATE_CLOSING) {
        return ESP_ERR_INVALID_STATE;
    }
    return softap_wifi_net_scan_start(scan_done, NULL);
}

static esp_err_t credentials_received(
    const char *ssid,
    const char *password,
    void *ctx)
{
    (void)ctx;
    if (s_state == PROVISION_STATE_CONNECTING ||
        s_state == PROVISION_STATE_CONNECTED ||
        s_state == PROVISION_STATE_CLOSING) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = credentials_store(ssid, password);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "已收到配网信息：SSID=\"%s\"，密码长度=%u",
             s_credentials.ssid, (unsigned)strlen(s_credentials.password));
    xEventGroupSetBits(s_event_group, EVENT_CONNECT_REQUEST);
    return ESP_OK;
}

static esp_err_t state_requested(void *ctx)
{
    (void)ctx;
    xEventGroupSetBits(s_event_group, EVENT_STATE_REQUEST);
    return ESP_OK;
}

static esp_err_t close_requested(void *ctx)
{
    (void)ctx;
    if (s_state != PROVISION_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    xEventGroupSetBits(s_event_group, EVENT_CLOSE_REQUEST);
    return ESP_OK;
}

static void connection_done(
    bool connected,
    const char *ip,
    uint8_t disconnect_reason,
    void *ctx)
{
    (void)ctx;
    if (connected) {
        snprintf(s_sta_ip, sizeof(s_sta_ip), "%s", (ip != NULL) ? ip : "");
        xEventGroupSetBits(s_event_group, EVENT_CONNECT_SUCCESS);
    } else {
        s_failure_reason = disconnect_reason;
        xEventGroupSetBits(s_event_group, EVENT_CONNECT_FAILED);
    }
}

static void manager_task(void *arg)
{
    (void)arg;
    while (true) {
        EventBits_t bits = xEventGroupWaitBits(
            s_event_group, EVENT_ALL, pdTRUE, pdFALSE, portMAX_DELAY);

        if ((bits & EVENT_CONNECT_REQUEST) != 0U) {
            xEventGroupClearBits(s_event_group, EVENT_STA_HAS_IP);
            s_sta_ip[0] = '\0';
            s_state = PROVISION_STATE_CONNECTING;
            (void)send_current_state();
            esp_err_t err = softap_wifi_net_connect(
                s_credentials.ssid,
                s_credentials.password,
                connection_done,
                NULL);
            if (err != ESP_OK) {
                s_failure_reason = err;
                s_state = PROVISION_STATE_FAILED;
                ESP_LOGE(TAG, "启动 STA 连接失败：%s", esp_err_to_name(err));
                (void)send_current_state();
            }
        }
        if ((bits & EVENT_CONNECT_SUCCESS) != 0U) {
            s_state = PROVISION_STATE_CONNECTED;
            xEventGroupSetBits(s_event_group, EVENT_STA_HAS_IP);
            (void)send_current_state();
        }
        if ((bits & EVENT_CONNECT_FAILED) != 0U) {
            s_state = PROVISION_STATE_FAILED;
            xEventGroupClearBits(s_event_group, EVENT_STA_HAS_IP);
            (void)send_current_state();
        }
        if ((bits & EVENT_STATE_REQUEST) != 0U) {
            (void)send_current_state();
        }
        if ((bits & EVENT_CLOSE_REQUEST) != 0U &&
            s_state == PROVISION_STATE_CONNECTED) {
            s_state = PROVISION_STATE_CLOSING;
            (void)send_current_state();
            vTaskDelay(pdMS_TO_TICKS(500));
            (void)softap_wifi_http_stop();
            esp_err_t err = softap_wifi_net_finish_provisioning();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "关闭 SoftAP 失败：%s", esp_err_to_name(err));
            }
            memset(s_credentials.password, 0, sizeof(s_credentials.password));
        }
    }
}

static esp_err_t component_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "擦除 NVS 失败");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t softap_wifi_cfg_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(component_nvs_init(), TAG, "初始化 NVS 失败");

    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(manager_task, "softap_cfg", 4096, NULL, 3, &s_manager_task) != pdPASS) {
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = softap_wifi_net_init();
    if (err != ESP_OK) {
        goto fail;
    }

    const softap_wifi_http_callbacks_t callbacks = {
        .on_scan = scan_requested,
        .on_credentials = credentials_received,
        .on_state = state_requested,
        .on_close = close_requested,
        .ctx = NULL,
    };
    err = softap_wifi_http_start(&callbacks);
    if (err != ESP_OK) {
        (void)softap_wifi_net_deinit();
        goto fail;
    }

    s_state = PROVISION_STATE_READY;
    s_failure_reason = 0;
    s_sta_ip[0] = '\0';
    s_started = true;
    ESP_LOGI(TAG, "SoftAP 网页配网已启动");
    return ESP_OK;

fail:
    vTaskDelete(s_manager_task);
    s_manager_task = NULL;
    vEventGroupDelete(s_event_group);
    s_event_group = NULL;
    return err;
}

esp_err_t softap_wifi_cfg_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }

    esp_err_t http_err = softap_wifi_http_stop();
    esp_err_t net_err = softap_wifi_net_deinit();
    if (s_manager_task != NULL) {
        vTaskDelete(s_manager_task);
        s_manager_task = NULL;
    }
    if (s_event_group != NULL) {
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
    }

    memset(&s_credentials, 0, sizeof(s_credentials));
    s_state = PROVISION_STATE_READY;
    s_started = false;
    return (http_err != ESP_OK) ? http_err : net_err;
}

esp_err_t softap_wifi_cfg_get_sta_ip(void)//(char *out_ip, size_t out_ip_size)
{
    // if (out_ip == NULL) {
    //     return ESP_ERR_INVALID_ARG;
    // }
    // if (out_ip_size < sizeof(s_sta_ip)) {
    //     return ESP_ERR_INVALID_SIZE;
    // }
    if (!s_started || s_event_group == NULL ||
        (xEventGroupGetBits(s_event_group) & EVENT_STA_HAS_IP) == 0U) {
        //out_ip[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    //snprintf(out_ip, out_ip_size, "%s", s_sta_ip);
    return ESP_OK;
}
