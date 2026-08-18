#include "my_ap_wifi.h"

#define TAG "ap_html"
#define TAG_WIFI "ap_wifi"

static char* index_html = NULL;
#define INDEX_HTML_PATH "/spiffs/apcfg.html"

//AP模式下的SSID名称
static const char* ap_ssid_name = "ESP32";
static const char* ap_password = "12345678";

//http服务器句柄
httpd_handle_t http_ws_server = NULL;
//连接的客户端fds
static int client_sockfd = -1;

//接收到ap配网的ssid和密码
static char current_ssid[32];
static char current_password[64];

static EventGroupHandle_t   apcfg_event = NULL;
#define APCFG_BIT   (BIT0)

static SemaphoreHandle_t scan_sem = NULL;



/** 从spiffs中加载html页面到内存
 * @param 无
 * @return 无 
*/
static char* initi_web_page_buffer(void)
{
    //定义挂载点
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",            //挂载点
        .partition_label = "html",         //分区名称
        .max_files = 5,                    //最大打开的文件数
        .format_if_mount_failed = false    //挂载失败是否执行格式化
        };
    //挂载spiffs
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
    //查找文件是否存在
    struct stat st;
    if (stat(INDEX_HTML_PATH, &st))
    {
        ESP_LOGE(TAG, "apcfg.html not found");
        return NULL;
    }
    //打开html文件并且读取到内存中
    char* page = (char*)malloc(st.st_size + 1);
    if(!page)
    {
        return NULL;
    }
    memset(page,0,st.st_size + 1);
    FILE *fp = fopen(INDEX_HTML_PATH, "r");
    if (fread(page, st.st_size, 1, fp) == 0)
    {
        free(page);
        page = NULL;
        ESP_LOGE(TAG, "fread failed");
    }
    fclose(fp);
    ESP_LOGI(TAG, "apcfg.html size = %d", st.st_size);
    return page;
}

esp_err_t web_ws_send(uint8_t* data, int len)
{
    ESP_LOGI(TAG,"WS send len = %d",len);
    if (client_sockfd < 0 || http_ws_server == NULL) {
        ESP_LOGE(TAG, "WS skip: fd=%d server=%p", client_sockfd, http_ws_server);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG,"WS data");
    // 复制一份数据，避免原始数据被提前释放
    uint8_t *buf = malloc(len);
    if (!buf) return ESP_ERR_NO_MEM;
    memcpy(buf, data, len);

    httpd_ws_frame_t pkt;
    memset(&pkt, 0, sizeof(httpd_ws_frame_t));
    pkt.payload = buf;
    pkt.len = len;
    pkt.type = HTTPD_WS_TYPE_TEXT;
    pkt.final = true; 

    esp_err_t ret = httpd_ws_send_data(http_ws_server, client_sockfd, &pkt);
    free(buf);
    ESP_LOGI(TAG, "WS send ret = %s (%d)", esp_err_to_name(ret), ret);
    return ret;
}

static void wifi_scan_finish_handle(int numbers,wifi_ap_record_t *ap_records)
{
    cJSON* root = cJSON_CreateObject();
    cJSON* wifilist_js = cJSON_AddArrayToObject(root,"wifi_list");
    for(int i = 0;i < numbers;i++)
    {
        cJSON* wifi_js = cJSON_CreateObject();
        cJSON_AddStringToObject(wifi_js,"ssid",(char*)ap_records[i].ssid);
        cJSON_AddNumberToObject(wifi_js,"rssi",ap_records[i].rssi);
        if(ap_records[i].authmode == WIFI_AUTH_OPEN)
            cJSON_AddBoolToObject(wifi_js,"encrypted",0);
        else
            cJSON_AddBoolToObject(wifi_js,"encrypted",1);
        cJSON_AddItemToArray(wifilist_js,wifi_js);
    }
    char* data = cJSON_Print(root);
    ESP_LOGI(TAG,"WS data:%s",data);
    web_ws_send((uint8_t*)data,strlen(data));
    cJSON_free(data);
    cJSON_Delete(root);
}



static void scan_task(void* param)
{
    uint16_t number = 5;
    wifi_ap_record_t *ap_info = malloc(sizeof(wifi_ap_record_t)*number);
    uint16_t ap_count = 0;
    ESP_LOGI(TAG,"Start wifi scan");
    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_LOGI(TAG, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, number);
    wifi_scan_finish_handle(number,ap_info);
    free(ap_info);
    xSemaphoreGive(scan_sem);
    vTaskDelete(NULL);
}

esp_err_t wifi_manager_scan(void)
{
    if(!scan_sem)
    {
        scan_sem = xSemaphoreCreateBinary();
        xSemaphoreGive(scan_sem);
    }
    if(pdTRUE == xSemaphoreTake(scan_sem,0))
    {
        //清除上次的扫描信息
        esp_wifi_clear_ap_list();
        //启动一个扫描任务
        if(pdTRUE == xTaskCreatePinnedToCore(scan_task,"scan",8192,NULL,3,NULL,0))
            return ESP_OK;
    }
    return ESP_FAIL;
}

/** ws接收回调函数
 * @param payload 数据
 * @param len 数据长度
 * @return 无 
*/
static void ws_receive_handle(uint8_t* payload,int len)
{
    cJSON* root = cJSON_Parse((char*)payload);
    if(root)
    {
        cJSON* scan_js = cJSON_GetObjectItem(root,"scan");
        cJSON* ssid_js = cJSON_GetObjectItem(root,"ssid");
        cJSON* password_js = cJSON_GetObjectItem(root,"password");
        if(scan_js)
        {
            char* scan_value = cJSON_GetStringValue(scan_js);
            if(strcmp(scan_value,"start") == 0)
            {
                wifi_manager_scan();
            }
        }
        if(ssid_js && password_js)
        {
            char* ssid = cJSON_GetStringValue(ssid_js);
            char* password = cJSON_GetStringValue(password_js);
            snprintf(current_ssid,sizeof(current_ssid),"%s",ssid);
            snprintf(current_password,sizeof(current_password),"%s",password);
            ESP_LOGI(TAG,"Receive ssid:%s,password:%s,now stop http server",current_ssid,current_password);
            //此回调函数里面由websocket底层调用，不宜直接调用关闭服务器操作
            xEventGroupSetBits(apcfg_event,APCFG_BIT);  
        }
    }
    else
    {
        ESP_LOGE(TAG,"Receive invaild json");
    }
}

// static void wifi_event_handler (void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
// {
//     if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
//     {
//         printf ("wifi 启动 \n");
//         wifi_mode_t mode;
//         esp_wifi_get_mode(&mode);
//         if(mode == WIFI_MODE_STA)
//         {
//             printf ("wifi 连接 \n");
//             esp_wifi_connect();         //启动WIFI连接
//         }
//     }
//     else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
//     {
//         printf ("wifi 重连 \n");
//         wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *) event_data;
//         ESP_LOGE ("wifi", "Disconnect reason = % d", disconn->reason);
//         vTaskDelay (5000 /portTICK_PERIOD_MS);// 等待 5 秒
//         esp_wifi_connect ();// 连接 wifi
//     }
//     else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
//     {
//         printf ("wifi 连接成功 \n");
//     }
//     /*ap 事件*/
//     else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
//         printf("设备接入\n");
//     } 
//     // 事件：设备断开AP热点
//     else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {;
//         printf("设备断开\n");
//     }

// }


// static void ap_wifi_apsta_init (void)
// {
//     ESP_ERROR_CHECK (esp_netif_init ());// 初始化 tcpip 栈
//     ESP_ERROR_CHECK (esp_event_loop_create_default ());// 硬件创建事件循环


//     esp_netif_create_default_wifi_sta ();// 创建默认的 wifi station 模式
//     esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap ();// 创建默认的 wifi ap 模式

//     wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT ();// 默认配置
//     ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));// 初始化 wifi
//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));// 设置 wifi 模式为 ap station
    
//     ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&wifi_event_handler, NULL, NULL));
//     ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,&wifi_event_handler, NULL, NULL));
    
//     //wifi ap 的配置
//     wifi_config_t wifi_ap_config =
//     {
//         .ap =
//         {
//             .authmode = WIFI_AUTH_WPA2_PSK,
//             .channel = 6,
//             .max_connection = 4,
//         },
//     };
//     //填充ap的ssid名称
//     snprintf((char*)wifi_ap_config.ap.ssid,31,"%s",ap_ssid_name);
//     wifi_ap_config.ap.ssid_len = strlen(ap_ssid_name);
//     //填充密码
//     snprintf((char*)wifi_ap_config.ap.password,63,"%s",ap_password);

//     ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&wifi_ap_config));// 设置 wifi ap 的配置
    
//     //如果是AP模式，则需要设置如下网络层信息
//     esp_netif_ip_info_t ipInfo;
//     IP4_ADDR(&ipInfo.ip, 192,168,100,1);    //本地的IP地址
//     IP4_ADDR(&ipInfo.gw, 192,168,100,1);    //网关IP地址
//     IP4_ADDR(&ipInfo.netmask, 255,255,255,0);   //子网掩码
//     esp_netif_dhcps_stop(esp_netif_ap);        //设置IP地址前需要停用DHCP服务
//     esp_netif_set_ip_info(esp_netif_ap, &ipInfo);    //设置IP地址
//     esp_netif_dhcps_start(esp_netif_ap);        //重新启动DHCP服务

//     ESP_ERROR_CHECK (esp_wifi_start ());// 启动 wifi
// }

// esp_err_t web_ws_stop(void)
// {
//     ESP_LOGI(TAG,"WS stop");
//     if(http_ws_server)
//     {
//         esp_err_t ret = httpd_stop(http_ws_server);
//         http_ws_server = NULL;
//         return ret;
//     }
//     return ESP_OK;
// }
 

/** 连接wifi
 * @param ssid
 * @param password
 * @return 成功/失败
*/
esp_err_t wifi_manager_connect(const char* ssid,const char* password)
{
    wifi_config_t wifi_config = 
    {
        .sta = 
        {
	        .threshold.authmode = WIFI_AUTH_WPA2_PSK,   //加密方式
        },
    };
    snprintf((char*)wifi_config.sta.ssid,31,"%s",ssid);
    snprintf((char*)wifi_config.sta.password,63,"%s",password);
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if(mode != WIFI_MODE_STA)
    {
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_start();
    }
    else
    {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_connect();
    }
    return ESP_OK;
}

static void ap_wifi_task(void* param)
{
    EventBits_t ev;
    while(1)
    {
        ev = xEventGroupWaitBits(apcfg_event,APCFG_BIT,pdTRUE,pdFALSE,pdMS_TO_TICKS(10*1000));
        if(ev &APCFG_BIT)
        {
            web_ws_stop();
            wifi_manager_connect(current_ssid,current_password);
        }
    }
}


// /** 当其他设备WS访问时触发此回调函数
//  * @param req http请求
//  * @return ESP_OK or ESP_FAIL
// */
// static esp_err_t handle_ws_req(httpd_req_t *req)
// {
//     // if (req->method == HTTP_GET)
//     // {
//     //     ESP_LOGI(TAG, "Handshake done, the new connection was opened");
//     //     //把套接字描述符保存下来，方便后续发送数据用
//     //     client_sockfd = httpd_req_to_sockfd(req);
//     //     ESP_LOGI(TAG,"Save client_fds:%d",client_sockfd);
//     //     return ESP_OK;
//     // }
//     int fd = httpd_req_to_sockfd(req);

//     if (httpd_ws_get_fd_info(req->handle, fd) !=
//         HTTPD_WS_CLIENT_WEBSOCKET) {
//         ESP_LOGE(TAG, "fd=%d is not an active WebSocket client", fd);
//         return ESP_FAIL;
//     }

//     // ESP-IDF 6.x 不会在握手时调用此 handler，
//     // 因此收到数据帧时保存当前连接
//     client_sockfd = fd;
//     ESP_LOGI(TAG, "WebSocket frame from fd=%d", client_sockfd);


//     httpd_ws_frame_t ws_pkt;
//     uint8_t *buf = NULL;
//     memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
//     esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
//     if (ret != ESP_OK)
//     {
//         return ret;
//     }
//     if (ws_pkt.len)
//     {
//         buf = calloc(1, ws_pkt.len + 1);
//         if (buf == NULL)
//         {
//             ESP_LOGE(TAG, "Failed to calloc memory for buf");
//             return ESP_ERR_NO_MEM;
//         }
//         ws_pkt.payload = buf;
//         ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
//         if (ret != ESP_OK)
//         {
//             ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
//             free(buf);
//             return ret;
//         }
//         ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
//     }
//     ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
//     if (ws_pkt.type == HTTPD_WS_TYPE_TEXT)
//     {
//         ws_receive_handle(ws_pkt.payload,ws_pkt.len);
//         free(buf);
//     }
//     return ESP_OK;
// }


// esp_err_t get_req_handler(httpd_req_t *req)
// {
//     esp_err_t response = ESP_FAIL;
//     if(index_html)
//     {
//         response = httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
//     }
//     return response;
// }


// esp_err_t   web_ws_start(void)
// {
//     //http和websocket初始化
//     httpd_config_t config = HTTPD_DEFAULT_CONFIG();
//     httpd_uri_t uri_get = 
//     {
//         .uri = "/",
//         .method = HTTP_GET,
//         .handler = get_req_handler,
//     };
//     httpd_uri_t ws = 
//     {
//         .uri = "/ws",
//         .method = HTTP_GET,
//         .handler = handle_ws_req,
//         .is_websocket = true
//     };

//     if (httpd_start(&http_ws_server, &config) == ESP_OK)
//     {
//         httpd_register_uri_handler(http_ws_server, &uri_get);
//         httpd_register_uri_handler(http_ws_server, &ws);
//     }

//     return ESP_OK;
// }

void ap_wifi_init(void)
{
    index_html = initi_web_page_buffer();
    ap_wifi_apsta_init();
    apcfg_event = xEventGroupCreate();
    xTaskCreatePinnedToCore(ap_wifi_task,"apcfg",4096,NULL,2,NULL,0);

    web_ws_start();
}