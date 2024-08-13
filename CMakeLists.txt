cmake_minimum_required(VERSION 3.19)


if(NOT IDF_TARGET STREQUAL linux)
  if(NOT DEFINED ENV{WIFI_SSID} OR NOT DEFINED ENV{WIFI_PASSWORD})
    message(FATAL_ERROR "Env variables WIFI_SSID and WIFI_PASSWORD must be set")
  endif()

  add_compile_definitions(WIFI_SSID="$ENV{WIFI_SSID}")
  add_compile_definitions(WIFI_PASSWORD="$ENV{WIFI_PASSWORD}")
endif()

if(NOT DEFINED ENV{LIVEKIT_URL})
  message(FATAL_ERROR "Env variable LIVEKIT_URL must be set")
endif()

add_compile_definitions(LIVEKIT_URL="$ENV{LIVEKIT_URL}")

set(COMPONENTS src)
set(EXTRA_COMPONENT_DIRS "src" "deps/esp-protocols/components/esp_websocket_client")

if(IDF_TARGET STREQUAL linux)
	add_compile_definitions(LINUX_BUILD=1)
  list(APPEND EXTRA_COMPONENT_DIRS
    $ENV{IDF_PATH}/examples/protocols/linux_stubs/esp_stubs
    "deps/esp-protocols/common_components/linux_compat/esp_timer"
    "deps/esp-protocols/common_components/linux_compat/freertos"
    )
endif()

if(NOT DEFINED ENV{SKIP_PROTOBUF_GENERATE})
  execute_process(
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMAND "protoc" "--c_out=deps/livekit-protocol-generated" "-I=./deps/livekit-protocol" "livekit_rtc.proto" "livekit_models.proto"
    COMMAND_ERROR_IS_FATAL ANY
  )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(src)
