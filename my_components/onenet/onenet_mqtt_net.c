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
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"

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

#define     MAX_DATA_BUFF   1024
//ota基础url
#define     ONENET_OTA_URL  "http://iot-api.heclouds.com/fuse-ota"
//token合法时间戳
#define     TOKEN_TIMESTAMP     1924833600

static uint8_t data_buff[MAX_DATA_BUFF];
//接收到的http数据长度
static size_t   data_buff_len = 0;
//ota升级任务id
static int  task_id = 0;
//要升级到的版本号
static char target_version[16] = {0}; 
//ota任务是否在运行
static bool ota_is_running = false;


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
 * 获取应用程序版本号
 * @param 无
 * @return 版本号
 */
const char* get_app_verion(void)
{
    static char app_version[32] = {0};
    if(app_version[0] == 0)
    {
        //获取当前运行的app分区信息
        const esp_partition_t *running = esp_ota_get_running_partition();
        esp_app_desc_t running_app_info;
        //根据app分区信息获取app描述信息
        esp_ota_get_partition_description(running, &running_app_info);
        snprintf(app_version,sizeof(app_version),"%s",running_app_info.version);
    }
    return app_version;
}


/**
 * 设置合法启动分区
 * @param vaild 是否合法
 * @return 无
 */
void set_app_vaild(int vaild)
{
    //获取当前运行的app分区信息
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    //获取当前运行的app状态
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) 
    {
        //如果是校验状态
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) 
        {
            if(vaild)
                esp_ota_mark_app_valid_cancel_rollback();   //设置成合法
            else
                esp_ota_mark_app_invalid_rollback_and_reboot(); //设置成非法并重启
        }
    }
}


/**
 * http事件回调函数
 * @param evt 包含http的数据
 * @return 错误码
 */
static esp_err_t http_client_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:    //错误事件
            //ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:    //连接成功事件
            //ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:    //发送头事件
            //ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:    //接收头事件
            //ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
            printf("%.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:    //接收数据事件
            {
                size_t copy_len = 0;
                ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
                printf("HTTP_EVENT_ON_DATA data=%.*s\r\n", evt->data_len,(char*)evt->data);
                if(evt->data_len > MAX_DATA_BUFF - data_buff_len)
                {
                    copy_len = MAX_DATA_BUFF - data_buff_len;
                }
                else
                {
                    copy_len = evt->data_len;
                }
                memcpy(&data_buff[data_buff_len],evt->data,copy_len);
                data_buff_len += copy_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:    //会话完成事件
            data_buff_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:    //断开事件
            //ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            data_buff_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            //ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
        default:
        break;
    }
    return ESP_OK;
}
/**
 * 发起http请求
 * @param url 请求地址
 * @param method 请求方法
 * @param payload 消息体内容
 * @return 错误码
 */
static esp_err_t onenet_ota_http_connect(const char* url,esp_http_client_method_t method,char* post_data)
{
    esp_http_client_config_t config =
    {
        .url = url,
        .event_handler = http_client_event_handler,
    };
    //初始化结构体
    esp_http_client_handle_t http_client = esp_http_client_init(&config);   //初始化http连接
    if(!http_client)
    {
        ESP_LOGI(TAG,"http_client init fail!");
        return ESP_FAIL;
    }

    char* token = (char*)malloc(256);
    memset(token,0,256);
    //计算token
    dev_token_generate(token,SIG_METHOD_SHA256,TOKEN_TIMESTAMP,ONENET_PRODUCT_ID,NULL,ONENET_ACCESS_KEY);
    ESP_LOGI(TAG,"user token:%s",token);
    //设置发送请求头
    esp_http_client_set_method(http_client, method);
    esp_http_client_set_header(http_client,"Content-Type","application/json");
    esp_http_client_set_header(http_client,"Authorization",token);
    esp_http_client_set_header(http_client,"host","iot-api.heclouds.com");
    if(post_data)
    {
        ESP_LOGI(TAG,"post data:%s",post_data);
        esp_http_client_set_post_field(http_client,post_data,strlen(post_data));
    }
    data_buff_len = 0;
    memset(data_buff,0,sizeof(data_buff));
    //esp_http_client_perform这句函数会阻塞，直到完整的http请求结束才返回
    esp_err_t err  = esp_http_client_perform(http_client);
    free(token);
    //清理操作
    esp_http_client_cleanup(http_client);
    return err;
}

/**
 * 查询升级任务状态
 * @param type = 1,说明是完整包，type=2,说明是差分包
 * @param version 当前设备版本
 * @return 错误码
 */
esp_err_t  onenet_ota_check_task(const char* type,const char* version)
{
    char url[256];
    esp_err_t ret = ESP_FAIL;
    snprintf(url,256,ONENET_OTA_URL"/%s/%s/check?type=%s&version=%s",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME,type,version);
    if(ESP_OK == onenet_ota_http_connect(url,HTTP_METHOD_GET,NULL))
    {
        cJSON *root =  cJSON_Parse((const char*)data_buff);
        if(root)
        {
            cJSON* code_js =  cJSON_GetObjectItem(root,"code");    //错误代码
            cJSON *data_js = cJSON_GetObjectItem(root,"data");
            cJSON* target_js = cJSON_GetObjectItem(data_js,"target");
            cJSON* tid_js = cJSON_GetObjectItem(data_js,"tid");
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
            {
                if(target_js && tid_js)    //我们感兴趣的只有任务id和目标版本号
                {
                    snprintf(target_version,sizeof(target_version),"%s",cJSON_GetStringValue(target_js));
                    task_id = cJSON_GetNumberValue(tid_js);    //取出任务id
                    ret = ESP_OK;
                }
            }
            else 
            {
                ESP_LOGI(TAG,"Check ota task invaild code");
            }
            cJSON_Delete(root);
        }
        else
        {
            ESP_LOGI(TAG,"Check ota task fail!");
            return ret;
        }
    }
    else
    {
        return ret;
    }
    return ret;
}

/**
 * 准确启动ota下载前的回调函数，在这里可以设置请求头
 * @param http_client http客户端句柄
 * @return 错误码
 */
static esp_err_t http_ota_init_callback(esp_http_client_handle_t http_client)
{
    static char token[256];
    memset(token,0,256);
    dev_token_generate(token,SIG_METHOD_SHA256,TOKEN_TIMESTAMP,ONENET_PRODUCT_ID,NULL,ONENET_ACCESS_KEY);
    ESP_LOGI(TAG,"user token:%s",token);
    //设置发送请求 
    esp_http_client_set_method(http_client, HTTP_METHOD_GET);
    esp_http_client_set_header(http_client,"Content-Type","application/json");
    esp_http_client_set_header(http_client,"Authorization",token);
    esp_http_client_set_header(http_client,"host","iot-api.heclouds.com");
    return ESP_OK;
}

/**
 * 启动ota下载
 * @param tid 升级任务id，通过查询升级任务可获取
 * @return 错误码
 */
esp_err_t onenet_ota_download(int tid)
{
    esp_err_t ota_finish_err = ESP_OK;
    char url[256];
    snprintf(url,sizeof(url),ONENET_OTA_URL"/%s/%s/%d/download",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME,tid);
    esp_http_client_config_t config =
    {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach, // 使用内置证书包验证
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,    //http配置结构体
        .http_client_init_cb = http_ota_init_callback,    //准备启动ota前的回调函数
    };
    ota_finish_err = esp_https_ota(&ota_config);    //执行ota下载
    if(ota_finish_err == ESP_OK)
    {
        ESP_LOGI(TAG, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");
    }
    else
    {
        ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
    }
    return ota_finish_err;
}

/**
 * 上报版本号
 * @param 无
 * @return 错误码
 */
esp_err_t onenet_ota_upload_version(void)
{
    //格式：{"s_version":"V1.3", "f_version": "V2.0"}
    char version_info[128];
    char url[256];
    esp_err_t ret = ESP_FAIL;
    //获取版本号
    const char* version = get_app_verion();
    //生成消息体内容（版本号）
    snprintf(version_info,sizeof(version_info),"{\"s_version\":\"%s\", \"f_version\": \"%s\"}",version,version);
    //计算url
    snprintf(url,256,ONENET_OTA_URL"/%s/%s/version",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    if(ESP_OK == onenet_ota_http_connect(url,HTTP_METHOD_POST,version_info))
    {
        cJSON *root = cJSON_Parse((const char*)data_buff);
        if(root)
        {
            cJSON* code_js =  cJSON_GetObjectItem(root,"code");
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
                ret = ESP_OK;
            cJSON_Delete(root);
        }
    }
    else
    {
        ESP_LOGI(TAG,"Upload version fail!");
        return ret;
    }
    return ret;
}

esp_err_t onenet_ota_upload_status(int tid,int step)
{
    char url[256];
    char payload[32];
    esp_err_t ret = ESP_FAIL;
    snprintf(url,256,ONENET_OTA_URL"/%s/%s/%d/status",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME,tid);
    snprintf(payload,sizeof(payload),"{\"step\":%d}",step);
    if(ESP_OK == onenet_ota_http_connect(url,HTTP_METHOD_POST,payload))
    {
        cJSON *root = cJSON_Parse((const char*)data_buff);
        if(root)
        {
            cJSON* code_js =  cJSON_GetObjectItem(root,"code");
            if(code_js && cJSON_GetNumberValue(code_js) == 0)
                ret = ESP_OK;
            cJSON_Delete(root);
        }
    }
    else
    {
        ESP_LOGI(TAG,"Upload status fail!");
        return ret;
    }
    return ret;
}


static void onenet_ota_task(void *param)
{
    esp_err_t ret;
    //上报当前版本号
    ret = onenet_ota_upload_version();
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG,"Upload version faild!");
        goto delete_ota_task;
    }
    //检测升级任务
    ret = onenet_ota_check_task("1",get_app_verion());
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG,"Check ota task faild!");
        goto delete_ota_task;
    }
    //上报任务升级状态
    ret = onenet_ota_upload_status(task_id,10);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG,"upload status faild!");
        goto delete_ota_task;
    }
    //进行http下载
    ret = onenet_ota_download(task_id);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG,"ota down load faild!");
        goto delete_ota_task;
    }
    //上报任务升级状态
    ret = onenet_ota_upload_status(task_id,100);
    //重启
    esp_restart();
    delete_ota_task:
    ota_is_running = false;
    vTaskDelete(NULL);
}

/**
 * 启动onenet ota升级流程
 * @param 无
 * @return 无
 */
void onenet_ota_start(void)
{
    if(ota_is_running)
        return;
    ota_is_running = true;
    ESP_LOGI(TAG,"Start OTA");
    xTaskCreatePinnedToCore(onenet_ota_task,"onenet_ota",8192,NULL,2,NULL,1);
}


/**
 * 返回OTA确认
 * @param code 错误码
 * @param message 信息
 * @return mqtt连接参数
 */
static void onenet_ota_ack(const char* id,int code,const char* message)
{
    char topic[128];
    snprintf(topic,sizeof(topic),"$sys/%s/%s/ota/inform_reply",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);

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
            cJSON* property_js = onenet_property_upload_dm();    //生成物模型数据cJSON
            char* data = cJSON_PrintUnformatted(property_js);    //提出cJSON的字符串格式数据
            onenet_post_property_data(data);   //上报数据
            cJSON_free(data);                 //释放生成的JSON字符串
            cJSON_Delete(property_js);        //释放cJSON对象
            set_app_vaild(true);
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
            else if(strstr(event->topic,"/ota/inform"))    //判断是否是ota/inform主题
            {
                cJSON *ota_js = cJSON_Parse(event->data);
                cJSON *id_js = cJSON_GetObjectItem(ota_js,"id");
                onenet_ota_ack(cJSON_GetStringValue(id_js),200,"success");
                cJSON_Delete(ota_js);
                
                onenet_ota_start();
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
    esp_mqtt_client_subscribe_single(s_onenet_client,topic,1);
    //订阅OTA主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/ota/inform",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    return esp_mqtt_client_subscribe_single(s_onenet_client,topic,1);
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
