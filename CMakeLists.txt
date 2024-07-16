cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{WIFI_SSID} OR NOT DEFINED ENV{WIFI_PASSWORD} OR NOT DEFINED ENV{WEBSOCKET_URI})
	message(FATAL_ERROR "Env variables WIFI_SSID, WIFI_PASSWORD and WEBSOCKET_URI must be set")
endif()

add_compile_definitions(WIFI_SSID="$ENV{WIFI_SSID}")
add_compile_definitions(WIFI_PASSWORD="$ENV{WIFI_PASSWORD}")
add_compile_definitions(WEBSOCKET_URI="$ENV{WEBSOCKET_URI}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(src)
