
set(src_dirs
            my_wifi_components
            softap_wifi)
 
set(include_dirs
            my_wifi_components
            softap_wifi)
 
set(requires
            esp_wifi
            nvs_flash
            spiffs
            esp_http_server
            )
 
idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
 
#component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)