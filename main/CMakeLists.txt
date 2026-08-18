idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS ".")

spiffs_create_partition_image(html ../my_components/softap_wifi/html FLASH_IN_PROJECT)
