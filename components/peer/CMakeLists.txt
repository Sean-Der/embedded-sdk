set(PEER_PROJECT_PATH "../../deps/libpeer")
file(GLOB CODES "${PEER_PROJECT_PATH}/src/*.c")

idf_component_register(
  SRCS ${CODES}
  INCLUDE_DIRS "${PEER_PROJECT_PATH}/src"
  REQUIRES mbedtls srtp json esp_netif
)

add_definitions("-DESP32 -DHTTP_DO_NOT_USE_CUSTOM_CONFIG -DMQTT_DO_NOT_USE_CUSTOM_CONFIG -DDISABLE_PEER_SIGNALING=true")

