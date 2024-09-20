#include <cJSON.h>
#include <esp_log.h>
#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <livekit_rtc.pb-c.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>

#include <vector>

#include "main.h"

#define WEBSOCKET_URI_SIZE 400
#define MTU_SIZE 1500
#define LIVEKIT_PROTOCOL_VERSION 3

static const char *SDP_TYPE_ANSWER = "answer";

SemaphoreHandle_t g_mutex;
char *offer_buffer = NULL;
char *answer_buffer = NULL;
char *ice_candidate_buffer = NULL;

static void on_icecandidate_task(char *description, void *user_data) {
  if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
    answer_buffer = strdup(description);
    xSemaphoreGive(g_mutex);
  }
}

void app_websocket_handle_livekit_response(Livekit__SignalResponse *packet) {
  switch (packet->message_case) {
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRICKLE: {
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRICKLE\n");

      auto parsed = cJSON_Parse(packet->trickle->candidateinit);
      if (!parsed) {
        ESP_LOGI(LOG_TAG, "failed to parse ice_candidate_init");
        return;
      }

      auto candidate_obj = cJSON_GetObjectItem(parsed, "candidate");
      if (!candidate_obj || !cJSON_IsString(candidate_obj)) {
        ESP_LOGI(LOG_TAG,
                 "failed to parse ice_candidate_init has no candidate\n");
        return;
      }

      ice_candidate_buffer = strdup(candidate_obj->valuestring);
      cJSON_Delete(parsed);
      break;
    }
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_OFFER:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_OFFER\n");
      offer_buffer = strdup(packet->offer->sdp);
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
          app_websocket_handle_livekit_response(new_response);
        }

        livekit__signal_response__free_unpacked(new_response, NULL);
        xSemaphoreGive(g_mutex);
      }
      break;
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGI(LOG_TAG, "WEBSOCKET_EVENT_ERROR");
      break;
  }
}

void app_websocket(void) {
  g_mutex = xSemaphoreCreateMutex();
  if (g_mutex == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create mutex.\n");
    return;
  }

  char ws_uri[WEBSOCKET_URI_SIZE];
  snprintf(ws_uri, WEBSOCKET_URI_SIZE,
           "%s/rtc?protocol=%d&access_token=%s&auto_subscribe=true",
           LIVEKIT_URL, LIVEKIT_PROTOCOL_VERSION, LIVEKIT_TOKEN);

  esp_websocket_client_config_t ws_cfg;
  memset(&ws_cfg, 0, sizeof(ws_cfg));

  ws_cfg.uri = ws_uri;
  ws_cfg.buffer_size = MTU_SIZE;
  ws_cfg.disable_pingpong_discon = true;
  ws_cfg.reconnect_timeout_ms = 1000;
  ws_cfg.network_timeout_ms = 1000;

  auto client = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                app_websocket_event_handler, (void *)client);
  esp_websocket_client_start(client);

  auto subscriber_peer_connection = app_create_peer_connection();

  peer_connection_onicecandidate(subscriber_peer_connection,
                                 on_icecandidate_task);

  pthread_t subscriber_peer_connection_thread_handle;
  pthread_create(&subscriber_peer_connection_thread_handle, NULL,
                 peer_connection_task, subscriber_peer_connection);

  while (true) {
    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
      if (answer_buffer != NULL) {
        Livekit__SignalRequest r = LIVEKIT__SIGNAL_REQUEST__INIT;
        Livekit__SessionDescription s = LIVEKIT__SESSION_DESCRIPTION__INIT;

        s.sdp = answer_buffer;
        s.type = (char *)SDP_TYPE_ANSWER;
        r.answer = &s;
        r.message_case = LIVEKIT__SIGNAL_REQUEST__MESSAGE_ANSWER;

        auto size = livekit__signal_request__get_packed_size(&r);
        auto *buffer = (uint8_t *)malloc(size);
        livekit__signal_request__pack(&r, buffer);
        auto len = esp_websocket_client_send_bin(client, (char *)buffer, size,
                                                 portMAX_DELAY);
        if (len == -1) {
          ESP_LOGI(LOG_TAG, "Failed to send answer");
        }

        free(answer_buffer);
        answer_buffer = NULL;
      }

      if (offer_buffer != NULL && ice_candidate_buffer != NULL) {
        peer_connection_add_ice_candidate(subscriber_peer_connection,
                                          ice_candidate_buffer);
        peer_connection_set_remote_description(subscriber_peer_connection,
                                               offer_buffer);

        free(offer_buffer);
        offer_buffer = NULL;

        free(ice_candidate_buffer);
        ice_candidate_buffer = NULL;
      }

      xSemaphoreGive(g_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
