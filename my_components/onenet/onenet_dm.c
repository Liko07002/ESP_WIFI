#include "onenet_dm.h"
#include <string.h>
#include "ws2812.h"

//灯的开关状态
static int led_status = 0;

//保存当前灯的亮度
static int led_brightness = 0;

//保存当前灯的RGB值
static int ws2812_red = 0;
static int ws2812_green = 0;
static int ws2812_blue = 0;


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
