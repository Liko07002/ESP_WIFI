
set(src_dirs
            my_wifi_components)
 
set(include_dirs
            my_wifi_components)
 
set(requires
            esp_wifi
            nvs_flash)
 
idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
 
#component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)