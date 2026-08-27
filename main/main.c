#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//#include "my_wifi.h"
//#include "my_ap_wifi.h"
#include "softap_wifi_cfg.h"
#include "onenet_mqtt_net.h"
#include "ws2812.h"
#include "led_strip.h"

static void wifi_status_changed(bool connected)
{
    printf("Wi-Fi 状态变化\n");
    static int count = 0;
    if(connected == 0)
    {
        softap_wifi_cfg_clear_credentials();
        count++;
    }
    else if(connected && count == 1)
    {
        onenet_start();
        count ++;
    }
    if(connected)
    {
        ws2812_set_color(0,0,10,0);
    }else
    {
        ws2812_set_color(0,10,0,0);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(ws2812_start());
    ESP_ERROR_CHECK(softap_wifi_cfg_set_status_callback(wifi_status_changed));
    ESP_ERROR_CHECK(softap_wifi_cfg_start());
    while(1)
    {
        printf("app_main_3\n");
        vTaskDelay (5000 /portTICK_PERIOD_MS);// 等待 1 秒
    }

}
