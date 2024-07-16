cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{WIFI_SSID} OR NOT DEFINED ENV{WIFI_PASSWORD})
	message(FATAL_ERROR "Env variables WIFI_SSID and WIFI_PASSWORD must be set")
endif()

add_compile_definitions(WIFI_SSID="$ENV{WIFI_SSID}")
add_compile_definitions(WIFI_PASSWORD="$ENV{WIFI_PASSWORD}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(src)
