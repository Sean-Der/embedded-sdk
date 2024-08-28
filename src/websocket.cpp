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
#define WEBSOCKET_URI_SIZE 400
#define MTU_SIZE 1500

std::vector<Livekit__SignalRequest *> app_websocket_packets;
SemaphoreHandle_t app_websocket_mutex;

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
      ESP_LOGI(LOG_TAG, "WEBSOCKET_EVENT_DATA");
      if (data->op_code == 0x08 && data->data_len == 2) {
        return;
      }

      if (xSemaphoreTake(app_websocket_mutex, portMAX_DELAY) == pdTRUE) {
        auto new_request = livekit__signal_request__unpack(
            NULL, data->data_len, (uint8_t *)data->data_ptr);

        if (new_request == NULL) {
          ESP_LOGE(LOG_TAG, "Failed to decode SignalRequest message.\n");
        } else {
          app_websocket_packets.push_back(new_request);
        }

        xSemaphoreGive(app_websocket_mutex);
      }
      break;
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGI(LOG_TAG, "WEBSOCKET_EVENT_ERROR");
      break;
  }
}

void app_websocket(void) {
  app_websocket_mutex = xSemaphoreCreateMutex();
  if (app_websocket_mutex == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create mutex.\n");
    return;
  }

  char ws_uri[WEBSOCKET_URI_SIZE];
  snprintf(ws_uri, sizeof(ws_uri), "%s/rtc?access_token=%s", LIVEKIT_URL,
           LIVEKIT_TOKEN);

  esp_websocket_client_config_t ws_cfg;
  memset(&ws_cfg, 0, sizeof(ws_cfg));

  ws_cfg.uri = ws_uri;
  ws_cfg.buffer_size = MTU_SIZE;
  ws_cfg.disable_pingpong_discon = true;

  auto client = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                app_websocket_event_handler, (void *)client);

  esp_websocket_client_start(client);

  // Example sending data
  // Livekit__AddTrackRequest r LIVEKIT__ADD_TRACK_REQUEST__INIT;
  // size_t size = livekit__add_track_request__get_packed_size(&r);
  // uint8_t *buffer =(uint8_t*) malloc(size);
  // livekit__add_track_request__pack(&r,buffer);
  // esp_err_t err = esp_websocket_client_send_bin(client,(char*)buffer,size,
  // portMAX_DELAY); if (err != ESP_OK) {
  //   printf("Failed to send data: %s\n", esp_err_to_name(err));
  // } else {
  //   printf("Data sent successfully\n");
  // }

  while (true) {
    if (xSemaphoreTake(app_websocket_mutex, portMAX_DELAY) == pdTRUE) {
      if (!app_websocket_packets.empty()) {
        auto packet = app_websocket_packets.front();

        switch (packet->message_case) {
          case LIVEKIT__SIGNAL_REQUEST__MESSAGE__NOT_SET:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_REQUEST__MESSAGE__NOT_SET\n");
            break;
          case LIVEKIT__SIGNAL_REQUEST__MESSAGE_OFFER:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_REQUEST__MESSAGE_OFFER\n");
            break;
          case LIVEKIT__SIGNAL_REQUEST__MESSAGE_ANSWER:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_REQUEST__MESSAGE_ANSWER\n");
            break;
          case LIVEKIT__SIGNAL_REQUEST__MESSAGE_TRICKLE:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_REQUEST__MESSAGE_TRICKLE\n");
            break;
          case LIVEKIT__SIGNAL_REQUEST__MESSAGE_ADD_TRACK:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_REQUEST__MESSAGE_ADD_TRACK\n");
            break;
          case LIVEKIT__SIGNAL_REQUEST__MESSAGE_MUTE:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_REQUEST__MESSAGE_MUTE\n");
            break;
          case LIVEKIT__SIGNAL_REQUEST__MESSAGE_SUBSCRIPTION:
            ESP_LOGI(LOG_TAG,
                     "LIVEKIT__SIGNAL_REQUEST__MESSAGE_SUBSCRIPTION\n");
            break;
          case LIVEKIT__SIGNAL_REQUEST__MESSAGE_TRACK_SETTING:
            ESP_LOGI(LOG_TAG,
                     "LIVEKIT__SIGNAL_REQUEST__MESSAGE_TRACK_SETTING\n");
            break;
          case LIVEKIT__SIGNAL_REQUEST__MESSAGE_LEAVE:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_REQUEST__MESSAGE_LEAVE\n");
            break;
          default:
            ESP_LOGI(LOG_TAG, "Unknown message type received.\n");
            break;
        }
        app_websocket_packets.erase(app_websocket_packets.begin());
        livekit__signal_request__free_unpacked(packet, NULL);
      }
      xSemaphoreGive(app_websocket_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

