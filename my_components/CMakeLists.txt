
set(src_dirs
            my_wifi_components
            softap_wifi
            onenet
            led_ws2812
            )
 
set(include_dirs
            my_wifi_components
            softap_wifi
            onenet
            led_ws2812
            )
 
set(requires
            esp_wifi
            lwip
            nvs_flash
            spiffs
            esp_http_server
            mbedtls
            mqtt
            led_strip
            esp_http_client
            esp_https_ota
            app_update
            )
 
idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
 
#component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
