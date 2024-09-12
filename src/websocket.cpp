#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>

#include <vector>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "livekit_rtc.pb-c.h"
#include "peer.h"

static const char *LOG_TAG = "embedded-sdk";

#define STRING_BUFFER_SIZE 750
#define MTU_SIZE 1500
#define LIVEKIT_PROTOCOL_VERSION 3

std::vector<Livekit__SignalResponse *> app_websocket_packets;
SemaphoreHandle_t g_mutex;

static void *peer_connection_task(void *user_data) {
  PeerConnection *peer_connection = (PeerConnection *)user_data;

  while (1) {
    peer_connection_loop(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  pthread_exit(NULL);
  return NULL;
}

static void on_icecandidate_task(char *description, void *user_data) {
  printf("%s \n", description);
}

static void onconnectionstatechange_task(PeerConnectionState state,
                                         void *user_data) {
  ESP_LOGI(LOG_TAG, "PeerConnectionState: %s",
           peer_connection_state_to_string(state));
}

static void app_websocket_event_handler(void *handler_args,
                                        esp_event_base_t base, int32_t event_id,
                                        void *event_data) {
  esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      ESP_LOGI(LOG_TAG, "WEBSOCKET_EVENT_CONNECTED");
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
      ESP_LOGI(LOG_TAG, "WEBSOCKET_EVENT_DISCONNECTED");
      break;
    case WEBSOCKET_EVENT_DATA:
      if (data->op_code == 0x08 && data->data_len == 2) {
        return;
      }

      if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        auto new_response = livekit__signal_response__unpack(
            NULL, data->data_len, (uint8_t *)data->data_ptr);

        if (new_response == NULL) {
          ESP_LOGE(LOG_TAG, "Failed to decode SignalResponse message.\n");
        } else {
          app_websocket_packets.push_back(new_response);
        }

        xSemaphoreGive(g_mutex);
      }
      break;
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGI(LOG_TAG, "WEBSOCKET_EVENT_ERROR");
      break;
  }
}

void app_websocket_handle_livekit_response(
    char *string_buffer, PeerConnection *subscriber_peer_connection) {
  auto packet = app_websocket_packets.front();

  switch (packet->message_case) {
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRICKLE:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRICKLE\n");
      peer_connection_add_ice_candidate(subscriber_peer_connection,
                                        packet->trickle->candidateinit);
      peer_connection_set_remote_description(subscriber_peer_connection,
                                             string_buffer);
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_OFFER:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_OFFER\n");
      strncpy(string_buffer, packet->offer->sdp, STRING_BUFFER_SIZE);
      break;
    /* Logging/NoOp below */
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE__NOT_SET:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE__NOT_SET\n");
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_JOIN:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_JOIN\n");
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_ANSWER:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_ANSWER\n");
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_UPDATE:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_UPDATE\n");
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRACK_PUBLISHED:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRACK_PUBLISHED\n");
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_LEAVE:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_LEAVE\n");
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_MUTE:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_MUTE\n");
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_SPEAKERS_CHANGED:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_SPEAKERS_CHANGED\n");
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_ROOM_UPDATE:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_ROOM_UPDATE\n");
      break;
    default:
      ESP_LOGI(LOG_TAG, "Unknown message type received.\n");
  }
  app_websocket_packets.erase(app_websocket_packets.begin());
  livekit__signal_response__free_unpacked(packet, NULL);
}

void app_websocket(void) {
  g_mutex = xSemaphoreCreateMutex();
  if (g_mutex == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create mutex.\n");
    return;
  }

  char *string_buffer = (char *)calloc(1, STRING_BUFFER_SIZE);
  snprintf(string_buffer, STRING_BUFFER_SIZE,
           "%s/rtc?protocol=%d&access_token=%s&auto_subscribe=true",
           LIVEKIT_URL, LIVEKIT_PROTOCOL_VERSION, LIVEKIT_TOKEN);

  esp_websocket_client_config_t ws_cfg;
  memset(&ws_cfg, 0, sizeof(ws_cfg));

  ws_cfg.uri = string_buffer;
  ws_cfg.buffer_size = MTU_SIZE;
  ws_cfg.disable_pingpong_discon = true;
  ws_cfg.reconnect_timeout_ms = 1000;
  ws_cfg.network_timeout_ms = 1000;

  auto client = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                app_websocket_event_handler, (void *)client);
  esp_websocket_client_start(client);

  PeerConfiguration peer_connection_config = {
      .ice_servers =
          {
              {
                  .urls = "stun:stun.l.google.com:19302",
                  .username = NULL,
                  .credential = NULL,
              },
          },
      .audio_codec = CODEC_NONE,
      .video_codec = CODEC_NONE,
      .datachannel = DATA_CHANNEL_STRING,
      .onaudiotrack = NULL,
      .onvideotrack = NULL,
      .on_request_keyframe = NULL,
      .user_data = NULL,
  };

  peer_init();

  PeerConnection *subscriber_peer_connection =
      peer_connection_create(&peer_connection_config);

  peer_connection_onicecandidate(subscriber_peer_connection,
                                 on_icecandidate_task);
  peer_connection_oniceconnectionstatechange(subscriber_peer_connection,
                                             onconnectionstatechange_task);

  pthread_t subscriber_peer_connection_thread_handle;
  pthread_create(&subscriber_peer_connection_thread_handle, NULL,
                 peer_connection_task, subscriber_peer_connection);
  memset(string_buffer, 0, STRING_BUFFER_SIZE);

  while (true) {
    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
      if (!app_websocket_packets.empty()) {
        app_websocket_handle_livekit_response(string_buffer,
                                              subscriber_peer_connection);
      }
      xSemaphoreGive(g_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

