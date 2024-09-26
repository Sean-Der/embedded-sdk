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

#define WEBSOCKET_URI_SIZE 1024
#define ANSWER_BUFFER_SIZE 1024
#define MTU_SIZE 1500
#define LIVEKIT_PROTOCOL_VERSION 3

static const char *SDP_TYPE_ANSWER = "answer";

SemaphoreHandle_t g_mutex;
char *offer_buffer = NULL;
char *answer_buffer = NULL;
char *ice_candidate_buffer = NULL;

// answer_status is a FSM of the following states
// * 0 - NoOp, don't send an answer
// * 1 - Send an answer with audio removed
// * 2 - Send an answer with audio enabled
int answer_status = 0;
char *answer_ice_ufrag = NULL;
char *answer_ice_pwd = NULL;
char *answer_fingerprint = NULL;

// on_icecandidate_task holds lock because peer_connection_task is
// what causes it to be fired
static void on_icecandidate_task(char *description, void *user_data) {
  auto fingerprint = strstr(description, "a=fingerprint");
  answer_fingerprint =
      strndup(fingerprint, (int)(strchr(fingerprint, '\r') - fingerprint));

  auto iceUfrag = strstr(description, "a=ice-ufrag");
  answer_ice_ufrag =
      strndup(iceUfrag, (int)(strchr(iceUfrag, '\r') - iceUfrag));

  auto icePwd = strstr(description, "a=ice-pwd");
  answer_ice_pwd = strndup(icePwd, (int)(strchr(icePwd, '\r') - icePwd));
}

void *peer_connection_task(void *user_data) {
  PeerConnection *peer_connection = (PeerConnection *)user_data;
  while (1) {
    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
      if (offer_buffer != NULL) {
        auto s = peer_connection_get_state(peer_connection);
        if (s == PEER_CONNECTION_COMPLETED || ice_candidate_buffer != NULL) {
          if (ice_candidate_buffer != NULL) {
            peer_connection_add_ice_candidate(peer_connection,
                                              ice_candidate_buffer);
          }

          peer_connection_set_remote_description(peer_connection, offer_buffer);

          free(offer_buffer);
          offer_buffer = NULL;

          free(ice_candidate_buffer);
          ice_candidate_buffer = NULL;
        }
      }

      xSemaphoreGive(g_mutex);
    }

    peer_connection_loop(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  pthread_exit(NULL);
  return NULL;
}

void lk_websocket_handle_livekit_response(Livekit__SignalResponse *packet) {
  switch (packet->message_case) {
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRICKLE: {
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRICKLE\n");

      // Skip TCP ICE Candidates
      if (strstr(packet->trickle->candidateinit, "tcp") != NULL) {
        ESP_LOGD(LOG_TAG, "skipping tcp ice candidate");
        return;
      }

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

      if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        ice_candidate_buffer = strdup(candidate_obj->valuestring);
        xSemaphoreGive(g_mutex);
      }

      cJSON_Delete(parsed);
      break;
    }
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_OFFER:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_OFFER\n");

      if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        if (strstr(packet->offer->sdp, "m=audio")) {
          answer_status = 2;
        } else {
          answer_status = 1;
        }

        offer_buffer = strdup(packet->offer->sdp);
        xSemaphoreGive(g_mutex);
      }

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

static void lk_websocket_event_handler(void *handler_args,
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
    case WEBSOCKET_EVENT_DATA: {
      if (data->op_code == 0x08 && data->data_len == 2) {
        return;
      }

      auto new_response = livekit__signal_response__unpack(
          NULL, data->data_len, (uint8_t *)data->data_ptr);

      if (new_response == NULL) {
        ESP_LOGE(LOG_TAG, "Failed to decode SignalResponse message.\n");
#ifndef LINUX_BUILD
        esp_restart();
#endif
      } else {
        lk_websocket_handle_livekit_response(new_response);
      }

      livekit__signal_response__free_unpacked(new_response, NULL);

      break;
    }
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGI(LOG_TAG, "WEBSOCKET_EVENT_ERROR");
      break;
  }
}

void lk_websocket(const char *room_url, const char *token) {
  g_mutex = xSemaphoreCreateMutex();
  if (g_mutex == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create mutex.\n");
    return;
  }

  answer_buffer = (char *)calloc(1, ANSWER_BUFFER_SIZE);

  char ws_uri[WEBSOCKET_URI_SIZE];
  snprintf(ws_uri, WEBSOCKET_URI_SIZE,
           "%s/rtc?protocol=%d&access_token=%s&auto_subscribe=true", room_url,
           LIVEKIT_PROTOCOL_VERSION, token);
  ESP_LOGI(LOG_TAG, "WebSocket URI: %s", ws_uri);

  esp_websocket_client_config_t ws_cfg;
  memset(&ws_cfg, 0, sizeof(ws_cfg));

  ws_cfg.uri = ws_uri;
  ws_cfg.buffer_size = MTU_SIZE;
  ws_cfg.disable_pingpong_discon = true;
  ws_cfg.reconnect_timeout_ms = 1000;
  ws_cfg.network_timeout_ms = 1000;

  auto client = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                lk_websocket_event_handler, (void *)client);
  esp_websocket_client_start(client);

  auto subscriber_peer_connection = lk_create_peer_connection();

  peer_connection_onicecandidate(subscriber_peer_connection,
                                 on_icecandidate_task);

  pthread_t subscriber_peer_connection_thread_handle;
  pthread_create(&subscriber_peer_connection_thread_handle, NULL,
                 peer_connection_task, subscriber_peer_connection);

  while (true) {
    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
      if (answer_status != 0 && answer_ice_ufrag != NULL) {
        Livekit__SignalRequest r = LIVEKIT__SIGNAL_REQUEST__INIT;
        Livekit__SessionDescription s = LIVEKIT__SESSION_DESCRIPTION__INIT;

        lk_populate_answer(answer_buffer, answer_ice_ufrag, answer_ice_pwd,
                           answer_fingerprint, answer_status == 2);
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

        answer_status = 0;
      }

      xSemaphoreGive(g_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
