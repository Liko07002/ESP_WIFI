# OneNET 组件

该组件只负责 OneNET MQTT 通信、OneJSON 消息封装和 OTA，不包含灯光等业务逻辑。
应用只应包含 `onenet.h`；其余头文件属于组件内部实现。

## 配置

所有常用配置集中在 `onenet_config.h`：

- `ONENET_PRODUCT_ID`、`ONENET_DEVICE_NAME`、`ONENET_ACCESS_KEY`：设备身份。
- `ONENET_TOKEN_EXPIRE_TIMESTAMP`：固定 Unix 过期时间。当前设备没有校时功能，
  因此必须在该时间到期前更新配置并重新构建固件。
- `ONENET_MQTT_URI`、`ONENET_MQTT_PORT`：MQTT 地址和端口。
- `ONENET_OTA_BASE_URL`：OTA API 地址。
- `ONENET_OTA_PROGRESS_STEP_PERCENT`：下载进度上报步长。
- `ONENET_OTA_NETWORK_MAX_RETRIES`、`ONENET_OTA_RETRY_INTERVAL_MS`：临时网络错误重试策略。
- `ONENET_OTA_TASK_STACK`、`ONENET_OTA_CONFIRM_TASK_STACK`：下载与启动确认任务栈。

MQTT 默认保持原工程连接；OTA 默认使用 HTTPS，并通过 `esp_crt_bundle_attach`
挂载 ESP-IDF 内置 CA 证书包验证 OneNET 服务器。量产前仍建议将 MQTT 切换为 MQTTS。

组件不会输出 Access Key 或鉴权 Token。`ONENET_ENABLE_PAYLOAD_LOG` 默认关闭。

## 最小启动

Wi-Fi 首次取得 IP 后调用一次 `onenet_start()`。后续网络中断由 MQTT 客户端自动重连：

```c
static bool onenet_started;

static void wifi_status_changed(bool connected)
{
    if (connected && !onenet_started) {
        if (onenet_start() == ESP_OK) {
            onenet_started = true;
        }
    }
}
```

所有回调均可选；传入 `NULL` 可以取消注册。未注册任何回调不影响组件连接和上报。

## 连接状态

```c
static void onenet_status_changed(bool connected)
{
    if (connected) {
        /* 可在这里上报设备完整状态。 */
    }
}

ESP_ERROR_CHECK(onenet_set_status_callback(onenet_status_changed));
```

也可调用 `onenet_is_connected()` 主动查询状态。

## 属性设置

同一个平台数据包只调用一次回调，完整 `params` 会以只读 cJSON 对象传入：

```c
static esp_err_t property_set(const cJSON *params)
{
    const cJSON *item = params->child;
    while (item != NULL) {
        if (strcmp(item->string, "LightSwitch") == 0 && cJSON_IsBool(item)) {
            /* 处理开关。 */
        } else if (strcmp(item->string, "Brightness") == 0 && cJSON_IsNumber(item)) {
            /* 使用 item->valueint。 */
        } else if (strcmp(item->string, "RGBColor") == 0 && cJSON_IsObject(item)) {
            const cJSON *red = cJSON_GetObjectItemCaseSensitive(item, "Red");
            const cJSON *green = cJSON_GetObjectItemCaseSensitive(item, "Green");
            const cJSON *blue = cJSON_GetObjectItemCaseSensitive(item, "Blue");
            /* 校验三个字段后执行设备业务。 */
        }
        item = item->next;
    }
    return ESP_OK;
}

ESP_ERROR_CHECK(onenet_set_property_callback(property_set));
```

`params` 仅在回调执行期间有效，用户不得修改或释放。如需异步处理，应在回调中使用
`cJSON_Duplicate(params, true)` 创建副本，并由业务代码释放。

返回 `ESP_OK` 时组件回复成功 ACK；没有注册回调时回复未处理；其他错误回复失败。
回调运行于 MQTT 事件上下文，不要在其中长时间阻塞。

## 批量属性上报

```c
cJSON *params = cJSON_CreateObject();
cJSON_AddNumberToObject(params, "Brightness", 50);
cJSON_AddBoolToObject(params, "LightSwitch", true);

cJSON *rgb = cJSON_AddObjectToObject(params, "RGBColor");
cJSON_AddNumberToObject(rgb, "Red", 100);
cJSON_AddNumberToObject(rgb, "Green", 100);
cJSON_AddNumberToObject(rgb, "Blue", 100);

esp_err_t err = onenet_report_properties(params);
cJSON_Delete(params);
```

组件复制 `params` 并自动添加消息 ID、`version` 和上报 topic，不接管调用者对象的所有权。
未连接时返回 `ESP_ERR_INVALID_STATE`。

## OTA

收到 OTA 通知时组件会立即回复 ACK：

- 未注册 OTA 回调：自动启动升级。
- 已注册 OTA 回调：只通知应用，应用稍后调用 `onenet_ota_start()`。

```c
static volatile bool ota_pending;

static void ota_notified(void)
{
    ota_pending = true;
}

ESP_ERROR_CHECK(onenet_set_ota_callback(ota_notified));

/* 用户确认后： */
ESP_ERROR_CHECK(onenet_ota_start());
```

开始升级时会重新查询任务。下载期间按配置步长上报 0～100 的进度；临时网络错误会
自动重试，版本不符、镜像无效和内存不足等确定性错误立即失败并上报对应状态。

下载成功后保存任务 ID、切换分区并重启。新固件成功连接 OneNET、上报状态 201 后才会
标记有效并清除 OTA 自己的 NVS 待确认记录；确认前发生崩溃或看门狗重启时，ESP-IDF
bootloader 会回滚旧固件。该清理不会影响 `softap_wifi` 命名空间中的 Wi-Fi 凭据。
项目必须保留双 OTA 分区、`otadata` 分区以及
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`。
