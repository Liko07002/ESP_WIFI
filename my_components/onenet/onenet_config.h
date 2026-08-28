#ifndef ONENET_CONFIG_H
#define ONENET_CONFIG_H

/* OneNET 设备身份。量产时请为每台设备配置独立的设备名称和密钥。 */
#define ONENET_PRODUCT_ID                         "8C72xzC90h"
#define ONENET_DEVICE_NAME                        "ESP32_S3"
#define ONENET_ACCESS_KEY                         "WWGJdxX1A+c9rwHhWa//48urCX256Rdbi5+3J1Exvy8="

/* 固定 Unix 过期时间。设备未校时时只能使用固定值，到期前必须更新固件。 */
#define ONENET_TOKEN_EXPIRE_TIMESTAMP             1924833600U

/* MQTT 默认保持现有连接；OTA 使用 HTTPS 和 ESP-IDF 内置 CA 证书包。 */
#define ONENET_MQTT_URI                           "mqtt://mqtts.heclouds.com"
#define ONENET_MQTT_PORT                          1883
#define ONENET_MQTT_QOS                           1
#define ONENET_MQTT_RX_MAX_BYTES                  2048U

#define ONENET_OTA_BASE_URL                       "https://iot-api.heclouds.com/fuse-ota"
#define ONENET_OTA_HTTP_TIMEOUT_MS                15000
#define ONENET_OTA_HTTP_RESPONSE_MAX_BYTES        2048U
#define ONENET_OTA_PROGRESS_STEP_PERCENT          5U
#define ONENET_OTA_NETWORK_MAX_RETRIES            3U
#define ONENET_OTA_RETRY_INTERVAL_MS              10000U

#define ONENET_OTA_TASK_STACK                     8192U
#define ONENET_OTA_CONFIRM_TASK_STACK             8192U
#define ONENET_OTA_TASK_PRIORITY                  2U
#define ONENET_OTA_TASK_CORE                      1

#define ONENET_OTA_NVS_NAMESPACE                  "onenet_ota"
#define ONENET_OTA_NVS_PENDING_TID_KEY            "pending_tid"
#define ONENET_OTA_NVS_TARGET_VERSION_KEY         "target_ver"

/* 设为 1 才允许输出协议载荷；无论此项为何值都不会打印密钥或 Token。 */
#define ONENET_ENABLE_PAYLOAD_LOG                 0

#if ONENET_OTA_PROGRESS_STEP_PERCENT == 0 || ONENET_OTA_PROGRESS_STEP_PERCENT > 100
#error "ONENET_OTA_PROGRESS_STEP_PERCENT must be in the range 1..100"
#endif

#if ONENET_OTA_NETWORK_MAX_RETRIES == 0
#error "ONENET_OTA_NETWORK_MAX_RETRIES must be at least 1"
#endif

#endif
