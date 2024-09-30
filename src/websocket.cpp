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
#define WEBSOCKET_BUFFER_SIZE 2048
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
void set_subscriber_status(int status) {
  ESP_LOGI(LOG_TAG, "Setting subscriber status to %d", status);
  subscriber_status = status;
}

extern int get_publisher_status();
extern void set_publisher_status(int status);
extern char *publisher_signaling_buffer;

// Offer + ICE Candidates. Captured in signaling thread
// and set PeerConnection thread
extern char *subscriber_offer_buffer;
extern char *ice_candidate_buffer;

extern char *subscriber_answer_ice_ufrag;

extern PeerConnection *subscriber_peer_connection;
extern PeerConnection *publisher_peer_connection;

static const char *request_message_to_string(
    Livekit__SignalRequest__MessageCase message_case) {
  switch (message_case) {
    case LIVEKIT__SIGNAL_REQUEST__MESSAGE_OFFER:
      return "OFFER";
    case LIVEKIT__SIGNAL_REQUEST__MESSAGE_ANSWER:
      return "ANSWER";
    case LIVEKIT__SIGNAL_REQUEST__MESSAGE_TRICKLE:
      return "TRICKLE";
    case LIVEKIT__SIGNAL_REQUEST__MESSAGE_ADD_TRACK:
      return "ADD_TRACK";
    case LIVEKIT__SIGNAL_REQUEST__MESSAGE_MUTE:
      return "MUTE";
    case LIVEKIT__SIGNAL_REQUEST__MESSAGE_SUBSCRIPTION:
      return "SUBSCRIPTION";
    case LIVEKIT__SIGNAL_REQUEST__MESSAGE_TRACK_SETTING:
      return "TRACK_SETTING";
    case LIVEKIT__SIGNAL_REQUEST__MESSAGE_LEAVE:
      return "LEAVE";
    default:
      ESP_LOGI(LOG_TAG, "Unknown message type %d", message_case);
      return "UNKNOWN";
  }
}

static const char *response_message_to_string(
    Livekit__SignalResponse__MessageCase message_case) {
  switch (message_case) {
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_JOIN:
      return "JOIN";
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_ANSWER:
      return "ANSWER";
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_OFFER:
      return "OFFER";
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRICKLE:
      return "TRICKLE";
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_UPDATE:
      return "UPDATE";
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRACK_PUBLISHED:
      return "TRACK_PUBLISHED";
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_LEAVE:
      return "LEAVE";
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_MUTE:
      return "MUTE";
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_SPEAKERS_CHANGED:
      return "SPEAKERS_CHANGED";
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_ROOM_UPDATE:
      return "ROOM_UPDATE";
    default:
      ESP_LOGI(LOG_TAG, "Unknown message type %d", message_case);
      return "UNKNOWN";
  }
}

void lk_websocket_handle_livekit_response(Livekit__SignalResponse *packet) {
  ESP_LOGI(LOG_TAG, "Recv %s",
           response_message_to_string(packet->message_case));
  switch (packet->message_case) {
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRICKLE: {
      // Skip TCP ICE Candidates
      if (strstr(packet->trickle->candidateinit, "tcp") != NULL) {
        ESP_LOGI(LOG_TAG, "skipping tcp ice candidate");
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
                 "failed to parse ice_candidate_init has no candidate");
        return;
      }

      ESP_LOGI(LOG_TAG, "Candidate: %d / %s", packet->trickle->target,
               candidate_obj->valuestring);
      if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        if (ice_candidate_buffer != NULL) {
          ESP_LOGI(LOG_TAG, "ice_candidate_buffer is not NULL");
        } else {
          ESP_LOGI(LOG_TAG, "buffering ICE candidate");
          ice_candidate_buffer = strdup(candidate_obj->valuestring);
        }
        xSemaphoreGive(g_mutex);
      }

      cJSON_Delete(parsed);
      break;
    }
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_OFFER:
      ESP_LOGI(LOG_TAG, "%s", packet->offer->sdp);

      ESP_LOGI(LOG_TAG, "Offer handler getting mutex");
      if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        ESP_LOGI(LOG_TAG, "Offer handler got mutex");
        if (strstr(packet->offer->sdp, "m=audio")) {
          set_subscriber_status(2);
        } else {
          set_subscriber_status(1);
        }

        subscriber_offer_buffer = strdup(packet->offer->sdp);
        xSemaphoreGive(g_mutex);
        ESP_LOGI(LOG_TAG, "Offer handler released mutex");
      }

      break;
    /* Logging/NoOp below */
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE__NOT_SET:
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_JOIN:
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_ANSWER:
      if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        publisher_signaling_buffer = strdup(packet->answer->sdp);
        set_publisher_status(4);
        xSemaphoreGive(g_mutex);
      }
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_UPDATE:
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRACK_PUBLISHED:
      if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
        set_publisher_status(2);
        xSemaphoreGive(g_mutex);
      }
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_LEAVE:
#ifndef LINUX_BUILD
      // esp_restart();  //????
#endif
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_MUTE:
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_SPEAKERS_CHANGED:
      break;
    case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_ROOM_UPDATE:
      break;
    default:
      ESP_LOGI(LOG_TAG, "Unknown message type received.");
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
      if (data->op_code != 0x2) {
        ESP_LOGD(LOG_TAG, "Message, opcode=%d, len=%d", data->op_code,
                 data->data_len);
        return;
      }

      auto new_response = livekit__signal_response__unpack(
          NULL, data->data_len, (uint8_t *)data->data_ptr);

      if (new_response == NULL) {
        ESP_LOGE(LOG_TAG, "Failed to decode SignalResponse message.");
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
  ESP_LOGI(LOG_TAG, "Send %s", request_message_to_string(r->message_case));
  auto size = livekit__signal_request__get_packed_size(r);
  auto *buffer = (uint8_t *)malloc(size);
  livekit__signal_request__pack(r, buffer);
  auto len = esp_websocket_client_send_bin(client, (char *)buffer, size,
                                           portMAX_DELAY);
  free(buffer);
  if (len == -1) {
    ESP_LOGI(LOG_TAG, "Failed to send message.");
  }
}

void lk_websocket(const char *room_url, const char *token) {
  g_mutex = xSemaphoreCreateMutex();
  if (g_mutex == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create mutex.");
    return;
  }

  subscriber_peer_connection = lk_create_peer_connection(/* isPublisher */ 0);
  publisher_peer_connection = lk_create_peer_connection(/* isPublisher */ 1);
  char *answer_buffer = (char *)malloc(ANSWER_BUFFER_SIZE);

  char *ws_uri = (char *)malloc(WEBSOCKET_URI_SIZE);
  snprintf(ws_uri, WEBSOCKET_URI_SIZE,
           "%s/rtc?protocol=%d&access_token=%s&auto_subscribe=true", room_url,
           LIVEKIT_PROTOCOL_VERSION, token);
  ESP_LOGI(LOG_TAG, "WebSocket URI: %s", ws_uri);

  esp_websocket_client_config_t ws_cfg;
  memset(&ws_cfg, 0, sizeof(ws_cfg));
  ws_cfg.uri = ws_uri;
  ws_cfg.buffer_size = WEBSOCKET_BUFFER_SIZE;
  ws_cfg.disable_pingpong_discon = true;
  ws_cfg.reconnect_timeout_ms = 1000;
  ws_cfg.network_timeout_ms = 1000;

  auto client = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                lk_websocket_event_handler, (void *)client);
  esp_websocket_client_start(client);
  free(ws_uri);

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
  TaskHandle_t publisher_pc_task_handle = NULL;
  TaskHandle_t subscriber_pc_task_handle = NULL;
  BaseType_t ret = xTaskCreatePinnedToCore(lk_subscriber_peer_connection_task,
                                           "lk_subscriber", 8192, NULL, 5,
                                           &subscriber_pc_task_handle, 1);
  assert(ret == pdPASS);
#endif

  while (true) {
    ESP_LOGI(LOG_TAG, "Websocket getting mutex");
    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
      ESP_LOGI(LOG_TAG, "Websocket got mutex");
      if (get_publisher_status() == 1 && SEND_AUDIO) {
        ESP_LOGI(LOG_TAG, "Sending add track request and starting publisher");
        Livekit__SignalRequest r = LIVEKIT__SIGNAL_REQUEST__INIT;
        Livekit__AddTrackRequest a = LIVEKIT__ADD_TRACK_REQUEST__INIT;

        a.cid = (char *)"microphone";
        a.name = (char *)"microphone";
        a.source = LIVEKIT__TRACK_SOURCE__MICROPHONE;

        r.add_track = &a;
        r.message_case = LIVEKIT__SIGNAL_REQUEST__MESSAGE_ADD_TRACK;

        lk_pack_and_send_signal_request(&r, client);
        set_publisher_status(0);
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
        StaticTask_t task_buffer;
        StackType_t *stack_memory = (StackType_t *)heap_caps_malloc(
            20000 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
        assert(stack_memory != NULL);
        publisher_pc_task_handle = xTaskCreateStaticPinnedToCore(
            lk_publisher_peer_connection_task, "lk_publisher", 20000, NULL, 7,
            stack_memory, &task_buffer, 0);
        assert(publisher_pc_task_handle != NULL);
#endif
        ESP_LOGI(LOG_TAG, "Sent add track request and started publisher");
      } else if (get_publisher_status() == 3) {
        ESP_LOGI(LOG_TAG, "Sending offer request for publisher");
        Livekit__SignalRequest r = LIVEKIT__SIGNAL_REQUEST__INIT;
        Livekit__SessionDescription s = LIVEKIT__SESSION_DESCRIPTION__INIT;

        s.sdp = publisher_signaling_buffer;
        s.type = (char *)SDP_TYPE_OFFER;
        r.offer = &s;
        r.message_case = LIVEKIT__SIGNAL_REQUEST__MESSAGE_OFFER;

        lk_pack_and_send_signal_request(&r, client);
        free(publisher_signaling_buffer);
        publisher_signaling_buffer = NULL;
        set_publisher_status(0);
        ESP_LOGI(LOG_TAG, "Sent offer request for publisher");
      }

      if (subscriber_status != 0 && subscriber_answer_ice_ufrag != NULL) {
        ESP_LOGI(LOG_TAG, "Sending answer request for subscriber");
        Livekit__SignalRequest r = LIVEKIT__SIGNAL_REQUEST__INIT;
        Livekit__SessionDescription s = LIVEKIT__SESSION_DESCRIPTION__INIT;

        lk_populate_answer(answer_buffer, ANSWER_BUFFER_SIZE,
                           subscriber_status == 2);
        s.sdp = answer_buffer;
        s.type = (char *)SDP_TYPE_ANSWER;
        r.answer = &s;
        r.message_case = LIVEKIT__SIGNAL_REQUEST__MESSAGE_ANSWER;

        lk_pack_and_send_signal_request(&r, client);
        set_subscriber_status(0);
        ESP_LOGI(LOG_TAG, "Sent answer request for subscriber");
      }

      xSemaphoreGive(g_mutex);
      ESP_LOGI(LOG_TAG, "Websocket released mutex");
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
