idf_component_register(
	SRCS
			 "../deps/livekit-protocol-generated/livekit_models.pb-c.c"
			 "../deps/livekit-protocol-generated/livekit_rtc.pb-c.c"
			 "main.cpp"
			 "websocket.cpp"
			 "wifi.cpp"
  INCLUDE_DIRS "." "../deps/livekit-protocol-generated")
