#include <esp_event.h>
#include <esp_log.h>
#include <pthread.h>

#include "main.h"

void *peer_connection_task(void *user_data) {
  PeerConnection *peer_connection = (PeerConnection *)user_data;
  while (1) {
    peer_connection_loop(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  pthread_exit(NULL);
  return NULL;
}

static void onconnectionstatechange_task(PeerConnectionState state,
                                         void *user_data) {
  ESP_LOGI(LOG_TAG, "PeerConnectionState: %s",
           peer_connection_state_to_string(state));
}

PeerConnection *app_create_peer_connection() {
  PeerConfiguration peer_connection_config = {
      .ice_servers = {},
      .audio_codec = CODEC_NONE,
      .video_codec = CODEC_NONE,
      .datachannel = DATA_CHANNEL_STRING,
      .onaudiotrack = NULL,
      .onvideotrack = NULL,
      .on_request_keyframe = NULL,
      .user_data = NULL,
  };

  PeerConnection *peer_connection =
      peer_connection_create(&peer_connection_config);

  peer_connection_oniceconnectionstatechange(peer_connection,
                                             onconnectionstatechange_task);

  return peer_connection;
}
