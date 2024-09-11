set(PEER_PROJECT_PATH "../../deps/libpeer")
file(GLOB CODES "${PEER_PROJECT_PATH}/src/*.c")

idf_component_register(
  SRCS ${CODES}
  INCLUDE_DIRS "${PEER_PROJECT_PATH}/src"
  REQUIRES mbedtls srtp json esp_netif
)

# Disable building of usrsctp
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libpeer/src/config.h INPUT_CONTENT)
string(REPLACE "#define HAVE_USRSCTP" "" MODIFIED_CONTENT ${INPUT_CONTENT})
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/libpeer/src/config.h ${MODIFIED_CONTENT})

if(NOT IDF_TARGET STREQUAL linux)
  add_definitions("-DESP32")
endif()
add_definitions("-DHTTP_DO_NOT_USE_CUSTOM_CONFIG -DMQTT_DO_NOT_USE_CUSTOM_CONFIG -DDISABLE_PEER_SIGNALING=true")
