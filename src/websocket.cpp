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
#define ANSWER_BUFFER_SIZE 1000
#define MTU_SIZE 1500
#define LIVEKIT_PROTOCOL_VERSION 3

static const char *SDP_TYPE_ANSWER = "answer";
static const char *SDP_TYPE_OFFER = "offer";

// Mutex used for all global state
SemaphoreHandle_t g_mutex;

// subscriber_status is a FSM of the following states
// * 0 - NoOp, don't send an answer
// * 1 - Send an answer with audio removed
// * 2 - Send an answer with audio enabled
int subscriber_status = 0;

extern int publisher_status;
extern char *publisher_signaling_buffer;

// Offer + ICE Candidates. Captured in signaling thread
// and set PeerConnection thread
extern char *subscriber_offer_buffer;
extern char *ice_candidate_buffer;

extern char *subscriber_answer_ice_ufrag;

extern PeerConnection *subscriber_peer_connection;
extern PeerConnection *publisher_peer_connection;

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
        if (ice_candidate_buffer != NULL) {
          return;
        }

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
          subscriber_status = 2;
        } else {
          subscriber_status = 1;
        }

        subscriber_offer_buffer = strdup(packet->offer->sdp);
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
      if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        publisher_signaling_buffer = strdup(packet->answer->sdp);
        publisher_status = 4;
        xSemaphoreGive(g_mutex);
      }

      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_UPDATE:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_UPDATE\n");
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRACK_PUBLISHED:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRACK_PUBLISHED\n");
      if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        publisher_status = 2;
        xSemaphoreGive(g_mutex);
      }

      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_LEAVE:
      ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_LEAVE\n");
#ifndef LINUX_BUILD
      esp_restart();
#endif
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
#ifndef LINUX_BUILD
      esp_restart();
#endif
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
#ifndef LINUX_BUILD
      esp_restart();
#endif
      break;
  }
}

void lk_pack_and_send_signal_request(const Livekit__SignalRequest *r,
                                     esp_websocket_client *client) {
  auto size = livekit__signal_request__get_packed_size(r);
  auto *buffer = (uint8_t *)malloc(size);
  livekit__signal_request__pack(r, buffer);
  auto len = esp_websocket_client_send_bin(client, (char *)buffer, size,
                                           portMAX_DELAY);
  free(buffer);
  if (len == -1) {
    ESP_LOGI(LOG_TAG, "Failed to send answer");
  }
}

void lk_websocket(void) {
  g_mutex = xSemaphoreCreateMutex();
  if (g_mutex == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create mutex.\n");
    return;
  }

  char *answer_buffer = (char *)calloc(1, ANSWER_BUFFER_SIZE);

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
                                lk_websocket_event_handler, (void *)client);
  esp_websocket_client_start(client);

  subscriber_peer_connection = lk_create_peer_connection(/* isPublisher */ 0);
  publisher_peer_connection = lk_create_peer_connection(/* isPublisher */ 1);

#ifdef LINUX_BUILD
  pthread_t subscriber_peer_connection_thread_handle;
  pthread_create(
      &subscriber_peer_connection_thread_handle, NULL,
      [](void *) -> void * {
        lk_subscriber_peer_connection_task(NULL);
        pthread_exit(NULL);
        return NULL;
      },
      NULL);
#else
  TaskHandle_t peer_connection_task_handle = NULL;
  StaticTask_t task_buffer;
  StackType_t *stack_memory = (StackType_t *)heap_caps_malloc(
      20000 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);

  xTaskCreatePinnedToCore(lk_subscriber_peer_connection_task, "lk_subscriber",
                          16384, NULL, 5, &peer_connection_task_handle, 1);
#endif

  while (true) {
    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
      if (publisher_status == 1 && SEND_AUDIO) {
        Livekit__SignalRequest r = LIVEKIT__SIGNAL_REQUEST__INIT;
        Livekit__AddTrackRequest a = LIVEKIT__ADD_TRACK_REQUEST__INIT;

        a.cid = (char *)"microphone";
        a.name = (char *)"microphone";
        a.source = LIVEKIT__TRACK_SOURCE__MICROPHONE;

        r.add_track = &a;
        r.message_case = LIVEKIT__SIGNAL_REQUEST__MESSAGE_ADD_TRACK;

        lk_pack_and_send_signal_request(&r, client);
        publisher_status = 0;

#ifdef LINUX_BUILD
        pthread_t publisher_peer_connection_thread_handle;
        pthread_create(
            &publisher_peer_connection_thread_handle, NULL,
            [](void *) -> void * {
              lk_publisher_peer_connection_task(NULL);
              pthread_exit(NULL);
              return NULL;
            },
            NULL);
#else
        if (stack_memory) {
          xTaskCreateStaticPinnedToCore(lk_publisher_peer_connection_task,
                                        "lk_publisher", 20000, NULL, 7,
                                        stack_memory, &task_buffer, 0);
        }
#endif

      } else if (publisher_status == 3) {
        Livekit__SignalRequest r = LIVEKIT__SIGNAL_REQUEST__INIT;
        Livekit__SessionDescription s = LIVEKIT__SESSION_DESCRIPTION__INIT;

        s.sdp = publisher_signaling_buffer;
        s.type = (char *)SDP_TYPE_OFFER;
        r.offer = &s;
        r.message_case = LIVEKIT__SIGNAL_REQUEST__MESSAGE_OFFER;

        lk_pack_and_send_signal_request(&r, client);
        free(publisher_signaling_buffer);
        publisher_signaling_buffer = NULL;
        publisher_status = 0;
      }

      if (subscriber_status != 0 && subscriber_answer_ice_ufrag != NULL) {
        Livekit__SignalRequest r = LIVEKIT__SIGNAL_REQUEST__INIT;
        Livekit__SessionDescription s = LIVEKIT__SESSION_DESCRIPTION__INIT;

        lk_populate_answer(answer_buffer, subscriber_status == 2);
        s.sdp = answer_buffer;
        s.type = (char *)SDP_TYPE_ANSWER;
        r.answer = &s;
        r.message_case = LIVEKIT__SIGNAL_REQUEST__MESSAGE_ANSWER;

        lk_pack_and_send_signal_request(&r, client);
        subscriber_status = 0;
      }

      xSemaphoreGive(g_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
