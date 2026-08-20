#ifndef WS2812_H
#define WS2812_H

#include "esp_err.h"
#define LED_STRIP_GPIO 48 // LED 灯带 GPIO 引脚
#define LED_STRIP_MAX_LEDS 1 // LED 数量

esp_err_t ws2812_set(uint8_t led_index, uint8_t r, uint8_t g, uint8_t b);
esp_err_t ws2812_clear(uint8_t led_index);
esp_err_t ws2812_start(void);
esp_err_t ws2812_set_brightness(uint8_t led_index, int brightness);
esp_err_t ws2812_set_color(uint8_t led_index, uint8_t r, uint8_t g, uint8_t b);
esp_err_t ws2812_open(uint8_t led_index);

#endif

