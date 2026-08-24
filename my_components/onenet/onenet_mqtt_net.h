#ifndef ONENET_MQTT_NET_H
#define ONENET_MQTT_NET_H
#include "esp_err.h"

//产品ID
#define  ONENET_PRODUCT_ID  "8C72xzC90h"

//产品秘钥
#define  ONENET_ACCESS_KEY  "WWGJdxX1A+c9rwHhWa//48urCX256Rdbi5+3J1Exvy8="

//设备名称
#define ONENET_DEVICE_NAME  "ESP32_S3"

//token有效时间（2030年12月30日）
#define TM_EXPIRE_TIME 1924833600
/**
 * 启动onenet连接
 * @return 错误码
 */
esp_err_t onenet_start(void);

/**
 * 物模型数据初始化
 * @param 无
 * @return 无
 */
void onenet_dm_init(void);

#endif