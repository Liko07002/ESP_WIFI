#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//#include "my_wifi.h"
//#include "my_ap_wifi.h"
#include "softap_wifi_cfg.h"
#include "onenet_mqtt_net.h"

// static EventGroupHandle_t wifi_net_event;
// #define EVENT_CONNECT_SUCCESS  BIT0

// static void wifi_net_task(void *pvParameters)
// {
//     softap_wifi_cfg_start();
//     wifi_net_event = xEventGroupCreate();
//     while(1)
//     {
//         if(softap_wifi_cfg_get_sta_ip() == ESP_OK)
//         {
//             xEventGroupSetBits(wifi_net_event,EVENT_CONNECT_SUCCESS);
//             onenet_start();
//             break;
//         }
//         vTaskDelay (1000 /portTICK_PERIOD_MS);// 等待 1 秒
//     };
//     vTaskDelete(NULL);
// }



void app_main(void)
{
    static int count = 0;
    softap_wifi_cfg_start();
    //xTaskCreate(wifi_net_task,"wifi_net_task",2048,NULL,5,NULL);
    while(1)
    {
        if(softap_wifi_cfg_get_sta_ip() == ESP_OK && count == 0)
        {
            //xEventGroupSetBits(wifi_net_event,EVENT_CONNECT_SUCCESS);
            onenet_start();
            count = 1;
        }
        printf("app_main\n");
        vTaskDelay (5000 /portTICK_PERIOD_MS);// 等待 1 秒
    }
}
