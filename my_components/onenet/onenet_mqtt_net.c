#include "onenet_mqtt_net.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "esp_log.h"
#include "cJSON.h"
#include "mqtt_client.h"
#include "onenet_token.h"
#include "onenet_dm.h"
#include "ws2812.h"
#include "cJSON.h"

#define TAG     "onenet_mqtt"
//连接成功标志位
static bool onenet_connected_flg = false;

//mqtt连接客户端
static esp_mqtt_client_handle_t s_onenet_client = NULL;

static void onenet_property_ack(const char* id,int code,const char* message);
static esp_err_t onenet_subscribe(void);

//灯的开关状态
static int led_status = 0;

//保存当前灯的亮度
static int led_brightness = 0;

//保存当前灯的RGB值
static int ws2812_red = 0;
static int ws2812_green = 0;
static int ws2812_blue = 0;



/**
 * 处理onenet下行的数据
 * @param property_js 包含下行数据的json
 * @return 无
 */
void onenet_property_handle(cJSON* property_js)
{
    cJSON *params_js = cJSON_GetObjectItem(property_js,"params");
    if(params_js)
    {
        cJSON *name_js = params_js->child;
        while(name_js)
        {
            if(strcmp(name_js->string,"LightSwitch") == 0)  //开关数据
            {
                if(cJSON_IsTrue(name_js))    //判断是否打开
                {
                    ws2812_open(0);
                }
                else
                {
                    ws2812_clear(0);
                }
            }
            else if(strcmp(name_js->string,"Brightness") == 0)  //亮度数据
            {
                //cJSON_GetNumberValue从一个cJSON的ITEM（键值对)中取出数值类型的值
                led_brightness = cJSON_GetNumberValue(name_js);
                ws2812_set_brightness(0,led_brightness);
            }
            else if(strcmp(name_js->string,"RGBColor") == 0)    //RGB数据
            {
                //取出键名为Red的值
                ws2812_red = cJSON_GetNumberValue(cJSON_GetObjectItem(name_js,"Red"));
                //取出键名为Green的值
                ws2812_green = cJSON_GetNumberValue(cJSON_GetObjectItem(name_js,"Green"));
                //取出键名为Blue的值
                ws2812_blue = cJSON_GetNumberValue(cJSON_GetObjectItem(name_js,"Blue"));
                //写入RBG值，每个灯都一样
                ws2812_set_color(0,ws2812_red,ws2812_green,ws2812_blue);
            }
            name_js = name_js->next;
        }
    }
}

/**
 * 生成上报所有数据的cJSON对象
 * @param 无
 * @return cJSON对象，包含所有属性值
 */
cJSON* onenet_property_upload_dm(void)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root,"id","123");
    cJSON_AddStringToObject(root,"version","1.0");
    cJSON* params_js = cJSON_AddObjectToObject(root,"params");
    //往params中填充灯开关值
    cJSON* light_js = cJSON_AddObjectToObject(params_js,"LightSwitch");
    cJSON_AddBoolToObject(light_js,"value",led_status);
    //往params中填充灯亮度值
    cJSON* brightness_js = cJSON_AddObjectToObject(params_js,"Brightness");
    cJSON_AddNumberToObject(brightness_js,"value",led_brightness);
    //往params中填充RGB值
    cJSON* color_js = cJSON_AddObjectToObject(params_js,"RGBColor");
    cJSON* color_value_js = cJSON_AddObjectToObject(color_js,"value");
    cJSON_AddNumberToObject(color_value_js,"Red",ws2812_red);
    cJSON_AddNumberToObject(color_value_js,"Green",ws2812_green);
    cJSON_AddNumberToObject(color_value_js,"Blue",ws2812_blue);
    return root;
}


/**
 * mqtt连接事件处理函数
 * @param event 事件参数
 * @return 无
 */
static void onenet_mqtt_event_handler(void* event_handler_arg,esp_event_base_t event_base,int32_t event_id,void* event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:  //连接成功
            ESP_LOGI(TAG, "Onenet mqtt 连接成功");
            onenet_connected_flg = true;    //将标志位设置为true
            onenet_subscribe();
            break;
        case MQTT_EVENT_DISCONNECTED:   //连接断开
            ESP_LOGI(TAG, "Onenet mqtt 连接断开");
            onenet_connected_flg = false;    //将标志位设置为false
            break;

        case MQTT_EVENT_SUBSCRIBED:     //收到订阅消息ACK
            ESP_LOGI(TAG, "Onenet mqtt 订阅成功 ack, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:   //收到解订阅消息ACK

            break;
        case MQTT_EVENT_PUBLISHED:      //收到发布消息ACK
            //ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            ESP_LOGI(TAG, "Onenet mqtt 发布成功 ack, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:           //收到数据消息
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            if(strstr(event->topic,"/property/set"))    //存在这个字符串说明是物模型属性设置
            {
                cJSON *property_js = cJSON_Parse(event->data);
                cJSON *id_js = cJSON_GetObjectItem(property_js,"id");    //提取id

                onenet_property_handle(property_js);    //调用onenet_dm中处理物模型数据的函数
                //返回ACK
                onenet_property_ack(cJSON_GetStringValue(id_js),200,"success");
                cJSON_Delete(property_js);    //释放cJSON对象
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            break;
    }
}

/**
 * 上报数据
 * @param data 数据
 * @return 错误
 */
static esp_err_t onenet_post_property_data(const char* data)
{
    if (!onenet_connected_flg)
        return ESP_FAIL;
    char topic[128];
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/post",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    ESP_LOGI(TAG,"Upload topic:%s,payload:%s",topic,data);
    return esp_mqtt_client_publish(s_onenet_client,topic,data,strlen(data),1,0);
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
    //密码，onenet平台使用token来作为密码，token直接使用dev_token_generate计算即可
    static char token[256];
    dev_token_generate(token, SIG_METHOD_SHA256, TM_EXPIRE_TIME, ONENET_PRODUCT_ID,ONENET_DEVICE_NAME, ONENET_ACCESS_KEY);
    mqtt_client.credentials.authentication.password = token;
    //将鉴权信息打印出来
    ESP_LOGI(TAG,"onenet connect->clientId:%s,username:%s,password:%s",mqtt_client.credentials.client_id,mqtt_client.credentials.username,mqtt_client.credentials.authentication.password);
    //设置mqtt的配置，返回一个mqtt句柄，此句柄后续用来发送数据、注册事件、断开连接使用
    s_onenet_client = esp_mqtt_client_init(&mqtt_client);
    //注册mqtt事件
    esp_mqtt_client_register_event(s_onenet_client, ESP_EVENT_ANY_ID, onenet_mqtt_event_handler, s_onenet_client);
    //启动mqtt连接，注意此函数会创建一个mqtt任务，并不会启动mqtt连接
    return esp_mqtt_client_start(s_onenet_client);
}

/**
 * 返回属性设置确认
 * @param code 错误码
 * @param message 信息
 * @return mqtt连接参数
 */
static void onenet_property_ack(const char* id,int code,const char* message)
{
    char topic[128];
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/set_reply",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    cJSON *reply_js = cJSON_CreateObject();
    cJSON_AddStringToObject(reply_js,"id",id);
    cJSON_AddNumberToObject(reply_js,"code",code);
    cJSON_AddStringToObject(reply_js,"message",message);
    char* data = cJSON_PrintUnformatted(reply_js);
    esp_mqtt_client_publish(s_onenet_client,topic,data,strlen(data),1,0); 
    cJSON_free(data);
    cJSON_Delete(reply_js);
}


/**
 * 订阅相关主题，有要订阅的主题可以放在这个函数
 * @param 无
 * @return 错误
 */
static esp_err_t onenet_subscribe(void)
{
    if (!onenet_connected_flg)
        return ESP_FAIL;
    char topic[128];
    //订阅上报属性回复主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/post/reply",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(s_onenet_client,topic,1);
    //订阅下行设置属性主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/set",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    return esp_mqtt_client_subscribe_single(s_onenet_client,topic,1);

    cJSON* property_js = onenet_property_upload_dm();    //生成物模型数据cJSON
    char* data = cJSON_PrintUnformatted(property_js);    //提出cJSON的字符串格式数据
    onenet_post_property_data(data);   //上报数据
    cJSON_free(data);                 //释放生成的JSON字符串
    cJSON_Delete(property_js);        //释放cJSON对象
}






/**
 * 物模型数据初始化
 * @param 无
 * @return 无
 */
void onenet_dm_init(void)
{
    //初始化ws2812
    ws2812_start();
}
