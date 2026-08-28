#include "softap_wifi_cfg.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "softap_wifi_config.h"
#include "softap_wifi_dns.h"
#include "softap_wifi_http.h"
#include "softap_wifi_net.h"
#include "softap_wifi_store.h"

#define TAG "softap_wifi_cfg"

#define EVENT_BOOT              BIT0
#define EVENT_NET_STATUS        BIT1
#define EVENT_CONNECT_REQUEST   BIT3
#define EVENT_STATE_REQUEST     BIT4
#define EVENT_CLOSE_REQUEST     BIT5
#define EVENT_FORCE_PORTAL      BIT6
#define EVENT_CLEAR_CREDENTIALS BIT7
#define EVENT_STOP              BIT8
#define EVENT_ALL (EVENT_BOOT | EVENT_NET_STATUS | \
                   EVENT_CONNECT_REQUEST | EVENT_STATE_REQUEST | EVENT_CLOSE_REQUEST | \
                   EVENT_FORCE_PORTAL | EVENT_CLEAR_CREDENTIALS | EVENT_STOP)

typedef enum {
    PROVISION_STATE_READY,
    PROVISION_STATE_CONNECTING,
    PROVISION_STATE_CONNECTED,
    PROVISION_STATE_FAILED,
    PROVISION_STATE_CLOSING,
} provision_state_t;

typedef enum {
    ATTEMPT_NONE,
    ATTEMPT_SAVED_INITIAL,
    ATTEMPT_RECOVERY,
    ATTEMPT_CANDIDATE,
    ATTEMPT_BACKGROUND,
} connection_attempt_t;

static EventGroupHandle_t s_event_group;
static QueueHandle_t s_net_status_queue;
static TaskHandle_t s_manager_task;
static softap_wifi_credentials_t s_saved_credentials;
static softap_wifi_credentials_t s_candidate_credentials;
static softap_wifi_status_cb_t s_status_callback;
static provision_state_t s_portal_state = PROVISION_STATE_READY;
static connection_attempt_t s_attempt;
static TickType_t s_attempt_deadline;
static TickType_t s_next_retry;
static TickType_t s_portal_close_deadline;
static char s_sta_ip[16];
static int s_failure_reason;
static volatile bool s_connected;
static bool s_report_initialized;
static bool s_reported_connected;
static bool s_saved_found;
static bool s_candidate_pending;
static bool s_retry_pending;
static bool s_portal_active;
static bool s_portal_close_pending;
static bool s_started;

typedef struct {
    bool connected;
    char ip[16];
    uint8_t disconnect_reason;
} net_status_message_t;

static bool time_reached(TickType_t now, TickType_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void publish_status(bool connected)
{
    if (s_report_initialized && s_reported_connected == connected) {
        return;
    }
    s_report_initialized = true;
    s_reported_connected = connected;
    if (s_status_callback != NULL) {
        s_status_callback(connected);
    }
}

static esp_err_t send_current_state(void)
{
    switch (s_portal_state) {
        case PROVISION_STATE_CONNECTING:
            return softap_wifi_http_send_status(
                "connecting", "正在连接路由器，请稍候…", NULL, 0);
        case PROVISION_STATE_CONNECTED:
            return softap_wifi_http_send_status(
                "connected", "设备已成功连接路由器", s_sta_ip, 0);
        case PROVISION_STATE_FAILED:
            return softap_wifi_http_send_status(
                "failed", "连接失败，请检查密码或信号后重试", NULL,
                s_failure_reason);
        case PROVISION_STATE_CLOSING:
            return softap_wifi_http_send_status(
                "closing", "配网已完成，热点即将关闭", s_sta_ip, 0);
        case PROVISION_STATE_READY:
        default:
            return softap_wifi_http_send_status(
                "ready", "请选择需要连接的 Wi-Fi", NULL, 0);
    }
}

static void scan_done(const wifi_ap_record_t *records, size_t count,
                      esp_err_t result, void *ctx)
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
        ESP_LOGW(TAG, "发送扫描结果失败：%s", esp_err_to_name(err));
    }
}

static esp_err_t scan_requested(void *ctx)
{
    (void)ctx;
    return s_candidate_pending
        ? ESP_ERR_INVALID_STATE
        : softap_wifi_net_scan_start(scan_done, NULL);
}

static esp_err_t credentials_store_ram(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) return ESP_ERR_INVALID_ARG;
    size_t ssid_len = strnlen(ssid, SOFTAP_WIFI_SSID_MAX_LEN + 1U);
    size_t password_len = strnlen(password, SOFTAP_WIFI_PASSWORD_MAX_LEN + 1U);
    if (ssid_len == 0U || ssid_len > SOFTAP_WIFI_SSID_MAX_LEN ||
        password_len > SOFTAP_WIFI_PASSWORD_MAX_LEN ||
        (password_len > 0U && password_len < 8U)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memset(&s_candidate_credentials, 0, sizeof(s_candidate_credentials));
    memcpy(s_candidate_credentials.ssid, ssid, ssid_len);
    memcpy(s_candidate_credentials.password, password, password_len);
    return ESP_OK;
}

static esp_err_t credentials_received(const char *ssid, const char *password, void *ctx)
{
    (void)ctx;
    if (s_candidate_pending) return ESP_ERR_INVALID_STATE;
    esp_err_t err = credentials_store_ram(ssid, password);
    if (err == ESP_OK) {
        s_candidate_pending = true;
        xEventGroupSetBits(s_event_group, EVENT_CONNECT_REQUEST);
    }
    return err;
}

static esp_err_t state_requested(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "收到配网页面状态查询");
    xEventGroupSetBits(s_event_group, EVENT_STATE_REQUEST);
    return ESP_OK;
}

static esp_err_t close_requested(void *ctx)
{
    (void)ctx;
    if (!s_connected) return ESP_ERR_INVALID_STATE;
    xEventGroupSetBits(s_event_group, EVENT_CLOSE_REQUEST);
    return ESP_OK;
}

static const softap_wifi_http_callbacks_t s_http_callbacks = {
    .on_scan = scan_requested,
    .on_credentials = credentials_received,
    .on_state = state_requested,
    .on_close = close_requested,
    .ctx = NULL,
};

static esp_err_t portal_start(void)
{
    if (s_portal_active) return ESP_OK;
    ESP_RETURN_ON_ERROR(softap_wifi_net_ap_start(), TAG, "启动配网热点失败");
    esp_err_t err = softap_wifi_dns_start();
    if (err != ESP_OK) {
        (void)softap_wifi_net_ap_stop();
        return err;
    }
    err = softap_wifi_http_start(&s_http_callbacks);
    if (err != ESP_OK) {
        (void)softap_wifi_dns_stop();
        (void)softap_wifi_net_ap_stop();
        return err;
    }
    s_portal_active = true;
    s_portal_state = PROVISION_STATE_READY;
    s_portal_close_pending = false;
    ESP_LOGI(TAG, "配网服务已启动");
    return ESP_OK;
}

static void portal_stop(void)
{
    if (!s_portal_active) return;
    (void)softap_wifi_http_stop();
    (void)softap_wifi_dns_stop();
    (void)softap_wifi_net_ap_stop();
    s_portal_active = false;
    s_portal_close_pending = false;
    s_portal_state = PROVISION_STATE_READY;
    ESP_LOGI(TAG, "配网服务已关闭");
}

static void net_status_changed(bool connected, const char *ip,
                               uint8_t disconnect_reason, void *ctx)
{
    (void)ctx;
    net_status_message_t message = {
        .connected = connected,
        .disconnect_reason = disconnect_reason,
    };
    if (ip != NULL) {
        snprintf(message.ip, sizeof(message.ip), "%s", ip);
    }
    if (s_net_status_queue != NULL && s_event_group != NULL) {
        xQueueOverwrite(s_net_status_queue, &message);
        xEventGroupSetBits(s_event_group, EVENT_NET_STATUS);
    }
}

static void begin_saved_attempt(connection_attempt_t attempt)
{
    s_attempt = attempt;
    s_attempt_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(SOFTAP_WIFI_CONNECT_TIMEOUT_MS);
    s_retry_pending = false;
    esp_err_t err = softap_wifi_net_connect(
        s_saved_credentials.ssid, s_saved_credentials.password);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "发起旧 Wi-Fi 连接失败：%s", esp_err_to_name(err));
    }
}

static void begin_background_retry(void)
{
    s_attempt = ATTEMPT_BACKGROUND;
    s_next_retry = xTaskGetTickCount() + pdMS_TO_TICKS(SOFTAP_WIFI_BACKGROUND_RETRY_MS);
}

static void handle_connected(void)
{
    bool candidate_succeeded = s_attempt == ATTEMPT_CANDIDATE && s_candidate_pending;
    s_connected = true;
    s_attempt = ATTEMPT_NONE;
    s_retry_pending = false;

    if (candidate_succeeded) {
        esp_err_t err = softap_wifi_store_save(&s_candidate_credentials);
        if (err == ESP_OK) {
            s_saved_credentials = s_candidate_credentials;
            s_saved_found = true;
            ESP_LOGI(TAG, "新 Wi-Fi 凭据已保存");
        } else {
            ESP_LOGE(TAG, "保存新 Wi-Fi 凭据失败：%s", esp_err_to_name(err));
        }
        memset(&s_candidate_credentials, 0, sizeof(s_candidate_credentials));
        s_candidate_pending = false;
    }

    publish_status(true);
    if (s_portal_active && s_portal_state != PROVISION_STATE_FAILED) {
        s_portal_state = PROVISION_STATE_CONNECTED;
        (void)send_current_state();
        s_portal_close_pending = true;
        s_portal_close_deadline = xTaskGetTickCount() +
            pdMS_TO_TICKS(SOFTAP_WIFI_PORTAL_AUTO_CLOSE_MS);
    }
}

static void handle_disconnect(void)
{
    s_connected = false;
    if (s_attempt == ATTEMPT_SAVED_INITIAL || s_attempt == ATTEMPT_RECOVERY ||
        s_attempt == ATTEMPT_CANDIDATE) {
        /* 驱动明确报告断开后再重试，避免连接或 DHCP 过程中重复调用连接 API。 */
        s_retry_pending = true;
        s_next_retry = xTaskGetTickCount() +
            pdMS_TO_TICKS(SOFTAP_WIFI_FAST_RETRY_INTERVAL_MS);
    } else if (s_attempt == ATTEMPT_NONE && s_saved_found) {
        ESP_LOGW(TAG, "网络连接中断，开始静默恢复");
        begin_saved_attempt(ATTEMPT_RECOVERY);
    }
}

static void handle_attempt_timeout(void)
{
    connection_attempt_t failed_attempt = s_attempt;
    s_attempt = ATTEMPT_NONE;
    s_retry_pending = false;

    if (failed_attempt == ATTEMPT_CANDIDATE) {
        ESP_LOGW(TAG, "新 Wi-Fi 在规定时间内未连接成功");
        s_candidate_pending = false;
        memset(&s_candidate_credentials, 0, sizeof(s_candidate_credentials));
        s_portal_state = PROVISION_STATE_FAILED;
        (void)send_current_state();
        if (s_saved_found) {
            begin_saved_attempt(ATTEMPT_RECOVERY);
            return;
        }
    }

    ESP_LOGW(TAG, "Wi-Fi 静默连接超时，进入配网模式");
    publish_status(false);
    if (portal_start() != ESP_OK) {
        ESP_LOGE(TAG, "进入配网模式失败");
    }
    if (s_saved_found) begin_background_retry();
}

static void handle_timers(void)
{
    TickType_t now = xTaskGetTickCount();
    if ((s_attempt == ATTEMPT_SAVED_INITIAL || s_attempt == ATTEMPT_RECOVERY ||
         s_attempt == ATTEMPT_CANDIDATE) && time_reached(now, s_attempt_deadline)) {
        handle_attempt_timeout();
        return;
    }
    if (s_retry_pending && !s_connected && time_reached(now, s_next_retry)) {
        s_retry_pending = false;
        (void)softap_wifi_net_reconnect();
    }
    if (s_attempt == ATTEMPT_BACKGROUND && s_saved_found && time_reached(now, s_next_retry)) {
        ESP_LOGI(TAG, "后台尝试连接已保存的 Wi-Fi");
        (void)softap_wifi_net_connect(
            s_saved_credentials.ssid, s_saved_credentials.password);
        s_next_retry = now + pdMS_TO_TICKS(SOFTAP_WIFI_BACKGROUND_RETRY_MS);
    }
    if (s_portal_close_pending && time_reached(now, s_portal_close_deadline)) {
        ESP_LOGI(TAG, "配网成功等待超时，自动关闭热点");
        portal_stop();
    }
}

static void manager_task(void *arg)
{
    (void)arg;
    bool running = true;
    while (running) {
        EventBits_t bits = xEventGroupWaitBits(
            s_event_group, EVENT_ALL, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));

        if ((bits & EVENT_BOOT) != 0U) {
            if (s_saved_found) begin_saved_attempt(ATTEMPT_SAVED_INITIAL);
            else {
                publish_status(false);
                if (portal_start() != ESP_OK) ESP_LOGE(TAG, "首次启动配网服务失败");
            }
        }
        if ((bits & EVENT_NET_STATUS) != 0U) {
            net_status_message_t message;
            if (xQueueReceive(s_net_status_queue, &message, 0) == pdTRUE) {
                if (message.connected) {
                    snprintf(s_sta_ip, sizeof(s_sta_ip), "%s", message.ip);
                    handle_connected();
                } else {
                    s_failure_reason = message.disconnect_reason;
                    handle_disconnect();
                }
            }
        }
        if ((bits & EVENT_CONNECT_REQUEST) != 0U && s_candidate_pending) {
            s_portal_state = PROVISION_STATE_CONNECTING;
            (void)send_current_state();
            s_attempt = ATTEMPT_CANDIDATE;
            s_retry_pending = false;
            s_attempt_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(SOFTAP_WIFI_CONNECT_TIMEOUT_MS);
            esp_err_t err = softap_wifi_net_connect(
                s_candidate_credentials.ssid, s_candidate_credentials.password);
            if (err != ESP_OK) {
                s_failure_reason = err;
                handle_attempt_timeout();
            }
        }
        if ((bits & EVENT_STATE_REQUEST) != 0U) (void)send_current_state();
        if ((bits & EVENT_CLOSE_REQUEST) != 0U && s_connected) {
            s_portal_state = PROVISION_STATE_CLOSING;
            (void)send_current_state();
            vTaskDelay(pdMS_TO_TICKS(500));
            portal_stop();
        }
        if ((bits & EVENT_FORCE_PORTAL) != 0U) {
            if (portal_start() != ESP_OK) ESP_LOGE(TAG, "手动启动配网服务失败");
        }
        if ((bits & EVENT_CLEAR_CREDENTIALS) != 0U) {
            esp_err_t err = softap_wifi_store_clear();
            if (err != ESP_OK) ESP_LOGE(TAG, "清除 Wi-Fi 凭据失败：%s", esp_err_to_name(err));
            memset(&s_saved_credentials, 0, sizeof(s_saved_credentials));
            s_saved_found = false;
            s_attempt = ATTEMPT_NONE;
            s_retry_pending = false;
            (void)softap_wifi_net_disconnect();
            publish_status(false);
            if (portal_start() != ESP_OK) ESP_LOGE(TAG, "恢复出厂后启动配网服务失败");
        }
        if ((bits & EVENT_STOP) != 0U) running = false;
        if (running) handle_timers();
    }
    portal_stop();
    s_manager_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t component_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区不可用，将擦除后重新初始化");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "擦除 NVS 失败");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t softap_wifi_cfg_set_status_callback(softap_wifi_status_cb_t callback)
{
    s_status_callback = callback;
    return ESP_OK;
}

esp_err_t softap_wifi_cfg_start(void)
{
    if (s_started) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(component_nvs_init(), TAG, "初始化 NVS 失败");

    bool found = false;
    ESP_RETURN_ON_ERROR(softap_wifi_store_load(&s_saved_credentials, &found),
                        TAG, "读取已保存的 Wi-Fi 失败");
    s_saved_found = found;
    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) return ESP_ERR_NO_MEM;
    s_net_status_queue = xQueueCreate(1, sizeof(net_status_message_t));
    if (s_net_status_queue == NULL) {
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = softap_wifi_net_init(net_status_changed, NULL);
    if (err != ESP_OK) goto fail;
    if (xTaskCreatePinnedToCore(manager_task, "softap_cfg", SOFTAP_WIFI_MANAGER_TASK_STACK,
                    NULL, SOFTAP_WIFI_MANAGER_TASK_PRIORITY,
                    &s_manager_task, SOFTAP_WIFI_MANAGER_TASK_CORE) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    s_connected = false;
    s_report_initialized = false;
    s_portal_active = false;
    s_candidate_pending = false;
    s_retry_pending = false;
    s_attempt = ATTEMPT_NONE;
    s_sta_ip[0] = '\0';
    s_started = true;
    xEventGroupSetBits(s_event_group, EVENT_BOOT);
    if (s_saved_found) {
        ESP_LOGI(TAG, "组件已启动，正在静默连接已保存的 Wi-Fi");
    } else {
        ESP_LOGI(TAG, "组件已启动，未发现已保存的 Wi-Fi");
    }
    return ESP_OK;

fail:
    (void)softap_wifi_net_deinit();
    vQueueDelete(s_net_status_queue);
    s_net_status_queue = NULL;
    vEventGroupDelete(s_event_group);
    s_event_group = NULL;
    return err;
}

esp_err_t softap_wifi_cfg_stop(void)
{
    if (!s_started) return ESP_OK;
    xEventGroupSetBits(s_event_group, EVENT_STOP);
    for (unsigned i = 0; s_manager_task != NULL && i < 200U; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_manager_task != NULL) {
        ESP_LOGW(TAG, "管理任务未及时退出，将强制停止");
        vTaskDelete(s_manager_task);
        s_manager_task = NULL;
    }
    esp_err_t err = softap_wifi_net_deinit();
    vQueueDelete(s_net_status_queue);
    s_net_status_queue = NULL;
    vEventGroupDelete(s_event_group);
    s_event_group = NULL;
    memset(&s_saved_credentials, 0, sizeof(s_saved_credentials));
    memset(&s_candidate_credentials, 0, sizeof(s_candidate_credentials));
    s_connected = false;
    s_started = false;
    ESP_LOGI(TAG, "Wi-Fi 管理组件已停止");
    return err;
}

bool softap_wifi_cfg_is_connected(void)
{
    return s_started && s_connected;
}

esp_err_t softap_wifi_cfg_get_sta_ip(char *out_ip, size_t out_ip_size)
{
    if (out_ip == NULL) return ESP_ERR_INVALID_ARG;
    if (out_ip_size < sizeof(s_sta_ip)) return ESP_ERR_INVALID_SIZE;
    if (!softap_wifi_cfg_is_connected()) {
        out_ip[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(out_ip, out_ip_size, "%s", s_sta_ip);
    return ESP_OK;
}

esp_err_t softap_wifi_cfg_start_provisioning(void)
{
    if (!s_started || s_event_group == NULL) return ESP_ERR_INVALID_STATE;
    xEventGroupSetBits(s_event_group, EVENT_FORCE_PORTAL);
    return ESP_OK;
}

esp_err_t softap_wifi_cfg_clear_credentials(void)
{
    if (!s_started || s_event_group == NULL) return ESP_ERR_INVALID_STATE;
    xEventGroupSetBits(s_event_group, EVENT_CLEAR_CREDENTIALS);
    return ESP_OK;
}
