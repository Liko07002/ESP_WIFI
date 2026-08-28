#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "softap_wifi_cfg.h"
#include "onenet.h"
#include "ws2812.h"
#include "led_strip.h"

static volatile bool ota_pending;
static bool onenet_started;

// static void ota_notified(void)
// {
//     ota_pending = true;
//     ESP_ERROR_CHECK(onenet_ota_start());
// }


static void onenet_status_changed(bool connected)
{
    if (connected) {
        cJSON *params = cJSON_CreateObject();
        cJSON_AddNumberToObject(params, "Brightness", 0);
        cJSON_AddBoolToObject(params, "LightSwitch", false);
        cJSON *rgb = cJSON_AddObjectToObject(params, "RGBColor");
        cJSON_AddNumberToObject(rgb, "Red", 0);
        cJSON_AddNumberToObject(rgb, "Green", 0);
        cJSON_AddNumberToObject(rgb, "Blue", 0);
        onenet_report_properties(params);
        cJSON_Delete(params);
    }
}

static esp_err_t property_set(const cJSON *params)
{
    const cJSON *item = params->child;
    while (item != NULL) {
        if (strcmp(item->string, "LightSwitch") == 0 && cJSON_IsBool(item)) {
            if(item->valueint)
            {
                ws2812_open(0);
            }else
            {
                ws2812_clear(0);
            }
        } else if (strcmp(item->string, "Brightness") == 0 && cJSON_IsNumber(item)) {
            ws2812_set_brightness(0,item->valueint);
        } else if (strcmp(item->string, "RGBColor") == 0 && cJSON_IsObject(item)) {
            const cJSON *red = cJSON_GetObjectItemCaseSensitive(item, "Red");
            const cJSON *green = cJSON_GetObjectItemCaseSensitive(item, "Green");
            const cJSON *blue = cJSON_GetObjectItemCaseSensitive(item, "Blue");
            if(red && green && blue)
            {
                ws2812_set_color(0,red->valueint,green->valueint,blue->valueint);
            }   
        }
        item = item->next;
    }
    return ESP_OK;
}

static void wifi_status_changed(bool connected)
{
    if (connected && !onenet_started) {
        if (onenet_start() == ESP_OK) {
            ESP_ERROR_CHECK(onenet_set_status_callback(onenet_status_changed));
            ESP_ERROR_CHECK(onenet_set_property_callback(property_set));
            //ESP_ERROR_CHECK(onenet_set_ota_callback(ota_notified));
            onenet_started = true;
        }
    }
}


void app_main(void)
{
    ESP_ERROR_CHECK(ws2812_start());
    ESP_ERROR_CHECK(softap_wifi_cfg_set_status_callback(wifi_status_changed));
    ESP_ERROR_CHECK(softap_wifi_cfg_start());
    while(1)
    {
        printf("app_main_4\n");
        vTaskDelay (5000 /portTICK_PERIOD_MS);// 等待 1 秒
    }

}
