# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# 新增：指定额外组件搜索目录，my_components下的一级子文件夹都是组件
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/my_components)

set(PROJECT_VER "1.0.3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESP_WIFI)
