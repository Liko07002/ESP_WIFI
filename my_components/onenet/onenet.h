#ifndef ONENET_H
#define ONENET_H

#include <stdbool.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** MQTT 连接状态变化回调。所有回调均可选。 */
typedef void (*onenet_status_cb_t)(bool connected);

/** 属性设置回调。params 只在回调期间有效，调用者不得释放或修改。 */
typedef esp_err_t (*onenet_property_set_cb_t)(const cJSON *params);

/** OTA 通知回调。注册后由应用决定何时调用 onenet_ota_start()。 */
typedef void (*onenet_ota_notify_cb_t)(void);

esp_err_t onenet_set_status_callback(onenet_status_cb_t callback);
esp_err_t onenet_set_property_callback(onenet_property_set_cb_t callback);
esp_err_t onenet_set_ota_callback(onenet_ota_notify_cb_t callback);

/** 启动/停止 OneNET；重复启动或停止是安全的。 */
esp_err_t onenet_start(void);
esp_err_t onenet_stop(void);

bool onenet_is_connected(void);

/** 批量上报属性。组件不接管 params 的所有权。 */
esp_err_t onenet_report_properties(const cJSON *params);

/** 异步启动 OTA。若任务已经运行则返回 ESP_ERR_INVALID_STATE。 */
esp_err_t onenet_ota_start(void);

#ifdef __cplusplus
}
#endif

#endif
