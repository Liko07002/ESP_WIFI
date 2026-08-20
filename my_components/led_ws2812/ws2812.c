#include "esp_log.h"
#include "led_strip.h"
#include "ws2812.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ws2812";

static led_strip_handle_t led_strip; // LED 灯带句柄

static void ws2812_init(void)
{
    ESP_LOGI(TAG, "配置 LED 灯带 GPIO 引脚为 %d", LED_STRIP_GPIO);
    // LED 灯带通用配置
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,                            // 设置 GPIO 引脚
        .max_leds = LED_STRIP_MAX_LEDS,                               // 设置 LED 数量
        //.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB, // 设置颜色格式
    };
    // RMT 后端特定配置
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // RMT 分辨率，10MHz
        .flags.with_dma = false,           // 禁用 DMA
    };
    // 创建 LED 灯带对象
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip); // 初始状态下清空灯带
}

esp_err_t ws2812_set_color(uint8_t led_index, uint8_t red, uint8_t green, uint8_t blue)
{
    led_strip_set_pixel(led_strip, led_index, red, green, blue);
    led_strip_refresh(led_strip);                 // 刷新灯带使颜色生效
    return ESP_OK;
}

esp_err_t ws2812_set_brightness(uint8_t led_index, int brightness)
{
    if (led_index >= LED_STRIP_MAX_LEDS) {
        ESP_LOGE(TAG, "LED index out of range");
        return ESP_ERR_INVALID_ARG;
    }
    if (brightness < 0 || brightness > 100) {
        ESP_LOGE(TAG, "Brightness out of range");
        return ESP_ERR_INVALID_ARG;
    }
    // 转换为 LED 灯带亮度值
    int red_brightness = 255 * brightness / 100;
    int green_brightness = 255 * brightness / 100;
    int blue_brightness = 255 * brightness / 100;
    
    ws2812_set_color(led_index, red_brightness, green_brightness, blue_brightness);
    led_strip_refresh(led_strip);                 // 刷新灯带使颜色生效
    return ESP_OK;
}

esp_err_t ws2812_open(uint8_t led_index)
{
   if (led_index >= LED_STRIP_MAX_LEDS) {
        ESP_LOGE(TAG, "LED index out of range");
        return ESP_ERR_INVALID_ARG;
    }else if (!led_index)
    {
        for (uint8_t i = 0; i < LED_STRIP_MAX_LEDS; i++) {
            led_strip_set_pixel(led_strip, i, 255, 255, 255);// 打开所有LED
        }
    }else
    {
        led_strip_set_pixel(led_strip, led_index - 1, 255, 255, 255);// 打开指定LED
    }
    led_strip_refresh(led_strip);                 // 刷新灯带使颜色生效
    return ESP_OK;
}

esp_err_t ws2812_clear(uint8_t led_index)
{
    if (led_index >= LED_STRIP_MAX_LEDS) {
        ESP_LOGE(TAG, "LED index out of range");
        return ESP_ERR_INVALID_ARG;
    }else if (!led_index)
    {
        led_strip_clear(led_strip);// 清空灯带
    }else
    {
        led_strip_set_pixel(led_strip, led_index - 1, 0, 0, 0);// 清空指定LED
    }
    led_strip_refresh(led_strip);                 // 刷新灯带使颜色生效
    return ESP_OK;
}

esp_err_t ws2812_start(void)
{
    ws2812_init(); // 配置 LED
    return ESP_OK;
}
