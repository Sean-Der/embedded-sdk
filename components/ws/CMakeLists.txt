set(COMMON_SRC
	"websocket.cpp"
	"esp32_websocket.cpp"
)

if(IDF_TARGET STREQUAL linux)
	idf_component_register(
		SRCS ${COMMON_SRC}
		INCLUDE_DIRS "."
		REQUIRES driver)
else()
	idf_component_register(
		SRCS ${COMMON_SRC}
	  	INCLUDE_DIRS "."
		REQUIRES driver esp_wifi nvs_flash)
endif()