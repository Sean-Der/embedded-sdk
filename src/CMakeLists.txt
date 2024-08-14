set(COMMON_SRC
	"../deps/livekit-protocol-generated/livekit_models.pb-c.c"
	"../deps/livekit-protocol-generated/livekit_rtc.pb-c.c"
	"websocket.cpp"
	"main.cpp")

if(IDF_TARGET STREQUAL linux)
	idf_component_register(
		SRCS ${COMMON_SRC}
		INCLUDE_DIRS "." "../deps/livekit-protocol-generated"
		REQUIRES protobuf-c esp_websocket_client)
else()
	idf_component_register(
		SRCS ${COMMON_SRC} "wifi.cpp"
	  INCLUDE_DIRS "." "../deps/livekit-protocol-generated"
		REQUIRES driver protobuf-c esp_wifi nvs_flash esp_websocket_client)
endif()

idf_component_get_property(lib espressif__esp_websocket_client COMPONENT_LIB)
target_compile_options(${lib} PRIVATE -Wno-error=format)
