set(COMMON_SRC
	"../deps/livekit-protocol-generated/livekit_models.pb-c.c"
	"../deps/livekit-protocol-generated/livekit_rtc.pb-c.c"
	"webrtc.cpp"
	"websocket.cpp"
	"main.cpp")

if(IDF_TARGET STREQUAL linux)
	idf_component_register(
		SRCS ${COMMON_SRC}
		INCLUDE_DIRS "." "../deps/livekit-protocol-generated"
		REQUIRES protobuf-c esp_websocket_client peer esp-libopus)
else()
	idf_component_register(
		SRCS ${COMMON_SRC} "wifi.cpp" "media.cpp"
	  INCLUDE_DIRS "." "../deps/livekit-protocol-generated"
		REQUIRES driver protobuf-c esp_wifi nvs_flash esp_websocket_client peer esp_psram esp-libopus)
endif()

idf_component_get_property(lib srtp COMPONENT_LIB)
target_compile_options(${lib} PRIVATE -Wno-error=incompatible-pointer-types)

idf_component_get_property(lib esp-libopus COMPONENT_LIB)
target_compile_options(${lib} PRIVATE -Wno-error=maybe-uninitialized)
target_compile_options(${lib} PRIVATE -Wno-error=stringop-overread)
