#include "onenet_mqtt_net.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "esp_log.h"
#include "cJSON.h"
#include "mqtt_client.h"
#include "onenet_token.h"

#define TAG     "onenet_mqtt"
//连接成功标志位
static bool onenet_connected_flg = false;

//mqtt连接客户端
static esp_mqtt_client_handle_t s_onenet_client = NULL;
/**
 * mqtt连接事件处理函数
 * @param event 事件参数
 * @return 无
 */
static void onenet_mqtt_event_handler(void* event_handler_arg,
                                        esp_event_base_t event_base,
                                        int32_t event_id,
                                        void* event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:  //连接成功
            ESP_LOGI(TAG, "Onenet mqtt connected");
            onenet_connected_flg = true;    //将标志位设置为true
            break;
        case MQTT_EVENT_DISCONNECTED:   //连接断开
            ESP_LOGI(TAG, "Onenet mqtt disconnected");
            onenet_connected_flg = false;    //将标志位设置为false
            break;

        case MQTT_EVENT_SUBSCRIBED:     //收到订阅消息ACK
            ESP_LOGI(TAG, "Onenet mqtt subscribed ack, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:   //收到解订阅消息ACK

            break;
        case MQTT_EVENT_PUBLISHED:      //收到发布消息ACK
            //ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            ESP_LOGI(TAG, "Onenet mqtt publish ack, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");

            break;
        default:
            break;
    }
}

/**
 * 启动mqtt连接
 * @param 无
 * @return 错误码
 */
esp_err_t onenet_start(void)
{
    esp_mqtt_client_config_t mqtt_client = {0};
    //onenet平台连接的mqtt地址
    mqtt_client.broker.address.uri = "mqtt://mqtts.heclouds.com";
    //onenet平台连接的mqtt端口号
    mqtt_client.broker.address.port = 1883;
    //client id用设备名称
    mqtt_client.credentials.client_id = ONENET_DEVICE_NAME;
    //用户名用产品id
    mqtt_client.credentials.username = ONENET_PRODUCT_ID;
    //token有效时间（2030年1月1日）
    #define TM_EXPIRE_TIME 1924833600
    //密码，onenet平台使用token来作为密码，token直接使用dev_token_generate计算即可
    static char token[256];
    dev_token_generate(token, SIG_METHOD_SHA256, TM_EXPIRE_TIME, ONENET_PRODUCT_ID,
         ONENET_DEVICE_NAME, ONENET_ACCESS_KEY);
    mqtt_client.credentials.authentication.password = token;
    //将鉴权信息打印出来
    ESP_LOGI(TAG,"onenet connect->clientId:%s,username:%s,password:%s",
        mqtt_client.credentials.client_id,mqtt_client.credentials.username,
        mqtt_client.credentials.authentication.password);
    //设置mqtt的配置，返回一个mqtt句柄，此句柄后续用来发送数据、注册事件、断开连接使用
    s_onenet_client = esp_mqtt_client_init(&mqtt_client);
    //注册mqtt事件
    esp_mqtt_client_register_event(s_onenet_client, ESP_EVENT_ANY_ID, 
        onenet_mqtt_event_handler, s_onenet_client);
    //启动mqtt连接，注意此函数会创建一个mqtt任务，并不会启动mqtt连接
    return esp_mqtt_client_start(s_onenet_client);
}