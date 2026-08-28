#include "onenet_ota.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "onenet_config.h"
#include "onenet_token.h"

#define TAG "onenet_ota"

typedef struct {
    char data[ONENET_OTA_HTTP_RESPONSE_MAX_BYTES + 1U];
    size_t length;
    bool overflow;
} http_response_t;

typedef struct {
    int tid;
    int size;
    char target[32];
} ota_job_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_ota_running;
static bool s_confirm_running;

static const char *current_version(void)
{
    const esp_app_desc_t *description = esp_app_get_description();
    return description != NULL ? description->version : "unknown";
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    http_response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL ||
        event->data == NULL || event->data_len <= 0) return ESP_OK;
    size_t available = ONENET_OTA_HTTP_RESPONSE_MAX_BYTES - response->length;
    size_t copy_length = (size_t)event->data_len;
    if (copy_length > available) {
        copy_length = available;
        response->overflow = true;
    }
    if (copy_length > 0) {
        memcpy(response->data + response->length, event->data, copy_length);
        response->length += copy_length;
        response->data[response->length] = '\0';
    }
    return ESP_OK;
}

static esp_err_t make_product_token(char *token, size_t size)
{
    if (token == NULL || size < 256U) return ESP_ERR_INVALID_SIZE;
    memset(token, 0, size);
    return dev_token_generate(token, SIG_METHOD_SHA256,
                              ONENET_TOKEN_EXPIRE_TIMESTAMP,
                              ONENET_PRODUCT_ID, NULL,
                              ONENET_ACCESS_KEY) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t ota_http_request(const char *url,
                                  esp_http_client_method_t method,
                                  const char *body, http_response_t *response)
{
    if (url == NULL || response == NULL) return ESP_ERR_INVALID_ARG;
    memset(response, 0, sizeof(*response));
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = response,
        .timeout_ms = ONENET_OTA_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;
    char token[256];
    esp_err_t err = make_product_token(token, sizeof(token));
    if (err == ESP_OK) err = esp_http_client_set_method(client, method);
    if (err == ESP_OK) err = esp_http_client_set_header(client, "Content-Type", "application/json");
    if (err == ESP_OK) err = esp_http_client_set_header(client, "Authorization", token);
    if (err == ESP_OK && body != NULL) {
        err = esp_http_client_set_post_field(client, body, (int)strlen(body));
    }
    if (err == ESP_OK) err = esp_http_client_perform(client);
    if (err == ESP_OK && esp_http_client_get_status_code(client) / 100 != 2) err = ESP_FAIL;
    if (response->overflow) err = ESP_ERR_INVALID_SIZE;
    memset(token, 0, sizeof(token));
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t response_code_ok(const http_response_t *response)
{
    cJSON *root = cJSON_ParseWithLength(response->data, response->length);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    esp_err_t err = cJSON_IsNumber(code) && code->valueint == 0
        ? ESP_OK : ESP_FAIL;
    cJSON_Delete(root);
    return err;
}

static esp_err_t upload_version(void)
{
    char url[256];
    char body[128];
    snprintf(url, sizeof(url), "%s/%s/%s/version",
             ONENET_OTA_BASE_URL, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    snprintf(body, sizeof(body), "{\"s_version\":\"%s\",\"f_version\":\"%s\"}",
             current_version(), current_version());
    http_response_t *response = calloc(1, sizeof(*response));
    if (response == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = ota_http_request(url, HTTP_METHOD_POST, body, response);
    if (err == ESP_OK) err = response_code_ok(response);
    free(response);
    return err;
}

static esp_err_t check_job(ota_job_t *job)
{
    char url[320];
    snprintf(url, sizeof(url), "%s/%s/%s/check?type=1&version=%s",
             ONENET_OTA_BASE_URL, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME,
             current_version());
    http_response_t *response = calloc(1, sizeof(*response));
    if (response == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = ota_http_request(url, HTTP_METHOD_GET, NULL, response);
    if (err != ESP_OK) {
        free(response);
        return err;
    }
    cJSON *root = cJSON_ParseWithLength(response->data, response->length);
    free(response);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON *tid = cJSON_GetObjectItemCaseSensitive(data, "tid");
    const cJSON *target = cJSON_GetObjectItemCaseSensitive(data, "target");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(data, "size");
    if (!cJSON_IsNumber(code) || code->valueint != 0 || !cJSON_IsObject(data) ||
        !cJSON_IsNumber(tid) || !cJSON_IsString(target) || target->valuestring == NULL) {
        err = ESP_ERR_NOT_FOUND;
    } else {
        memset(job, 0, sizeof(*job));
        job->tid = tid->valueint;
        job->size = cJSON_IsNumber(size) ? size->valueint : 0;
        snprintf(job->target, sizeof(job->target), "%s", target->valuestring);
        err = ESP_OK;
    }
    cJSON_Delete(root);
    return err;
}

static esp_err_t upload_status(int tid, int step)
{
    char url[256];
    char body[32];
    snprintf(url, sizeof(url), "%s/%s/%s/%d/status",
             ONENET_OTA_BASE_URL, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, tid);
    snprintf(body, sizeof(body), "{\"step\":%d}", step);
    http_response_t *response = calloc(1, sizeof(*response));
    if (response == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = ota_http_request(url, HTTP_METHOD_POST, body, response);
    if (err == ESP_OK) err = response_code_ok(response);
    free(response);
    return err;
}

static esp_err_t ota_http_init(esp_http_client_handle_t client)
{
    static char token[256];
    esp_err_t err = make_product_token(token, sizeof(token));
    if (err == ESP_OK) err = esp_http_client_set_header(client, "Authorization", token);
    if (err == ESP_OK) err = esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    return err;
}

static esp_err_t save_pending_job(const ota_job_t *job)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ONENET_OTA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(handle, ONENET_OTA_NVS_PENDING_TID_KEY, job->tid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, ONENET_OTA_NVS_TARGET_VERSION_KEY, job->target);
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t load_pending_job(ota_job_t *job)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ONENET_OTA_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    int32_t tid;
    size_t target_size = sizeof(job->target);
    err = nvs_get_i32(handle, ONENET_OTA_NVS_PENDING_TID_KEY, &tid);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, ONENET_OTA_NVS_TARGET_VERSION_KEY,
                          job->target, &target_size);
    }
    nvs_close(handle);
    if (err == ESP_OK) job->tid = tid;
    return err;
}

static void clear_pending_job(void)
{
    nvs_handle_t handle;
    if (nvs_open(ONENET_OTA_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    (void)nvs_erase_key(handle, ONENET_OTA_NVS_PENDING_TID_KEY);
    (void)nvs_erase_key(handle, ONENET_OTA_NVS_TARGET_VERSION_KEY);
    (void)nvs_commit(handle);
    nvs_close(handle);
}

static int failure_status(esp_err_t err)
{
    if (err == ESP_ERR_NO_MEM) return 103;
    if (err == ESP_ERR_OTA_VALIDATE_FAILED) return 205;
    if (err == ESP_ERR_INVALID_VERSION) return 204;
    return 107;
}

static esp_err_t download_job(const ota_job_t *job)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/%s/%s/%d/download",
             ONENET_OTA_BASE_URL, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, job->tid);
    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = ONENET_OTA_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .http_client_init_cb = ota_http_init,
    };
    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
    if (err != ESP_OK) return err;

    esp_app_desc_t new_app;
    err = esp_https_ota_get_img_desc(handle, &new_app);
    if (err == ESP_OK && strcmp(new_app.version, job->target) != 0) {
        ESP_LOGE(TAG, "目标版本 %s 与镜像版本 %s 不一致", job->target, new_app.version);
        err = ESP_ERR_INVALID_VERSION;
    }
    int image_size = esp_https_ota_get_image_size(handle);
    if (image_size <= 0) image_size = job->size;
    unsigned last_reported = 0;
    while (err == ESP_OK || err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        err = esp_https_ota_perform(handle);
        int received = esp_https_ota_get_image_len_read(handle);
        if (received >= 0 && image_size > 0) {
            unsigned percent = (unsigned)(((uint64_t)received * 100U) / (unsigned)image_size);
            if (percent > 100U) percent = 100U;
            unsigned threshold = (percent / ONENET_OTA_PROGRESS_STEP_PERCENT) *
                                 ONENET_OTA_PROGRESS_STEP_PERCENT;
            if (threshold > last_reported && threshold < 100U) {
                if (upload_status(job->tid, (int)threshold) == ESP_OK) last_reported = threshold;
            }
        }
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
    }
    if (err == ESP_OK && !esp_https_ota_is_complete_data_received(handle)) {
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK) err = esp_https_ota_finish(handle);
    else (void)esp_https_ota_abort(handle);
    return err;
}

static void ota_task(void *argument)
{
    (void)argument;
    ota_job_t job;
    esp_err_t err = upload_version();
    if (err == ESP_OK) err = check_job(&job);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "查询 OTA 任务失败：%s", esp_err_to_name(err));
        goto done;
    }

    unsigned attempt = 0;
    (void)upload_status(job.tid, 0);
    do {
        attempt++;
        ESP_LOGI(TAG, "开始下载 OTA，任务 %d，第 %u 次尝试", job.tid, attempt);
        err = download_job(&job);
        if (err == ESP_OK || err == ESP_ERR_INVALID_ARG ||
            err == ESP_ERR_INVALID_VERSION ||
            err == ESP_ERR_OTA_VALIDATE_FAILED || err == ESP_ERR_NO_MEM) break;
        if (attempt < ONENET_OTA_NETWORK_MAX_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(ONENET_OTA_RETRY_INTERVAL_MS));
            ota_job_t refreshed;
            if (check_job(&refreshed) != ESP_OK || refreshed.tid != job.tid) break;
            job = refreshed;
        }
    } while (attempt < ONENET_OTA_NETWORK_MAX_RETRIES);

    if (err != ESP_OK) {
        int status = attempt >= ONENET_OTA_NETWORK_MAX_RETRIES
            ? 207 : failure_status(err);
        (void)upload_status(job.tid, status);
        ESP_LOGE(TAG, "OTA 失败：%s", esp_err_to_name(err));
        goto done;
    }
    (void)upload_status(job.tid, 100);
    (void)upload_status(job.tid, 101);
    err = save_pending_job(&job);
    if (err != ESP_OK) {
        (void)upload_status(job.tid, 206);
        ESP_LOGE(TAG, "保存 OTA 确认信息失败：%s", esp_err_to_name(err));
        goto done;
    }
    ESP_LOGI(TAG, "OTA 下载和校验完成，准备重启");
    esp_restart();

done:
    taskENTER_CRITICAL(&s_lock);
    s_ota_running = false;
    taskEXIT_CRITICAL(&s_lock);
    vTaskDelete(NULL);
}

esp_err_t onenet_ota_component_start(void)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_ota_running) {
        taskEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_ota_running = true;
    taskEXIT_CRITICAL(&s_lock);
    BaseType_t created = xTaskCreatePinnedToCore(
        ota_task, "onenet_ota", ONENET_OTA_TASK_STACK, NULL,
        ONENET_OTA_TASK_PRIORITY, NULL, ONENET_OTA_TASK_CORE);
    if (created != pdPASS) {
        taskENTER_CRITICAL(&s_lock);
        s_ota_running = false;
        taskEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void confirm_task(void *argument)
{
    (void)argument;
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    ota_job_t pending = {0};
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        load_pending_job(&pending) == ESP_OK) {
        if (strcmp(current_version(), pending.target) != 0) {
            ESP_LOGE(TAG, "当前版本与待确认 OTA 目标版本不一致");
            (void)upload_status(pending.tid, 206);
            clear_pending_job();
        } else {
            bool reported = false;
            for (unsigned attempt = 0;
                 attempt < ONENET_OTA_NETWORK_MAX_RETRIES; ++attempt) {
                if (upload_status(pending.tid, 201) == ESP_OK) {
                    reported = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(ONENET_OTA_RETRY_INTERVAL_MS));
            }
            if (reported) {
                esp_err_t err = state == ESP_OTA_IMG_PENDING_VERIFY
                    ? esp_ota_mark_app_valid_cancel_rollback() : ESP_OK;
                if (err == ESP_OK) {
                    clear_pending_job();
                    ESP_LOGI(TAG, "新固件已确认并向 OneNET 上报升级成功");
                } else {
                    ESP_LOGE(TAG, "确认新固件失败：%s", esp_err_to_name(err));
                }
            } else {
                ESP_LOGW(TAG, "上报 OTA 成功状态失败，保留待确认记录稍后重试");
            }
        }
    }
    taskENTER_CRITICAL(&s_lock);
    s_confirm_running = false;
    taskEXIT_CRITICAL(&s_lock);
    vTaskDelete(NULL);
}

void onenet_ota_on_mqtt_connected(void)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_confirm_running) {
        taskEXIT_CRITICAL(&s_lock);
        return;
    }
    s_confirm_running = true;
    taskEXIT_CRITICAL(&s_lock);
    if (xTaskCreate(confirm_task, "onenet_confirm",
                    ONENET_OTA_CONFIRM_TASK_STACK, NULL,
                    ONENET_OTA_TASK_PRIORITY, NULL) != pdPASS) {
        taskENTER_CRITICAL(&s_lock);
        s_confirm_running = false;
        taskEXIT_CRITICAL(&s_lock);
    }
}
