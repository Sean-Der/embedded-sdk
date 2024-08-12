if(IDF_TARGET STREQUAL linux)
else()
	idf_component_register(
		SRCS ${COMMON_SRC} "wifi.cpp"
	  	INCLUDE_DIRS "."
		REQUIRES driver esp_wifi nvs_flash
	)
endif()