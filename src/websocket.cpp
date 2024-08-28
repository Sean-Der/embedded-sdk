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
#include "peer_connection.h"

static const char *LOG_TAG = "embedded-sdk";
#define WEBSOCKET_URI_SIZE 316
#define MTU_SIZE 1500

std::vector<Livekit__SignalResponse *> app_websocket_packets;
SemaphoreHandle_t app_websocket_mutex;
SemaphoreHandle_t xSemaphore = NULL;

static TaskHandle_t xPcTaskHandle = NULL;
char ws_uri[WEBSOCKET_URI_SIZE];

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
        ESP_LOGI(LOG_TAG, "Received opcode=%d", data->op_code);
        ESP_LOGW(
          LOG_TAG,
          "Received closed message with code=%d",
          256 * data->data_ptr[0] + data->data_ptr[1]
        );
        return;
      }

      if (xSemaphoreTake(app_websocket_mutex, portMAX_DELAY) == pdTRUE) {
        Livekit__SignalResponse* new_request = livekit__signal_response__unpack(
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



PeerConnection *g_pc;
PeerConnectionState eState = PEER_CONNECTION_CLOSED;
int gDataChannelOpened = 0;

static char deviceid[32] = {0};
uint8_t mac[8] = {0};

PeerConfiguration config = {
  .ice_servers = {
  { .urls = "stun:stun.l.google.com:19302" }
  },
  .datachannel = DATA_CHANNEL_BINARY,
};

static void oniceconnectionstatechange(PeerConnectionState state, void *user_data) {

  ESP_LOGI(LOG_TAG, "PeerConnectionState: %d", state);
  eState = state;
  // not support datachannel close event
  if (eState != PEER_CONNECTION_COMPLETED) {
    gDataChannelOpened = 0;
  }
}

static void onmessage(char *msg, size_t len, void *userdata, uint16_t sid) {

  ESP_LOGI(LOG_TAG, "Datachannel message: %.*s", len, msg);
}

void onopen(void *userdata) {
 
  ESP_LOGI(LOG_TAG, "Datachannel opened");
  gDataChannelOpened = 1;
}

static void onclose(void *userdata) {
 
}


void peer_connection_task(void *arg) {

  ESP_LOGI(LOG_TAG, "peer_connection_task started");
  peer_connection_create_offer(g_pc);

  for(;;) {

    if (xSemaphoreTake(xSemaphore, portMAX_DELAY)) {
        peer_connection_loop(g_pc);
        xSemaphoreGive(xSemaphore);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void app_websocket(void) { 
  ESP_LOGI(LOG_TAG, "[APP] Free memory: %d bytes", (int)esp_get_free_heap_size());
  vTaskDelay(pdMS_TO_TICKS(10000));
  app_websocket_mutex = xSemaphoreCreateMutex();
  xSemaphore = xSemaphoreCreateMutex();
  if (app_websocket_mutex == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create mutex.\n");
    return;
  }

  snprintf(ws_uri, sizeof(ws_uri), "%s/rtc?auto_subscribe=1&access_token=%s", LIVEKIT_URL,
           LIVEKIT_TOKEN);

  esp_websocket_client_config_t ws_cfg;
  memset(&ws_cfg, 0, sizeof(ws_cfg));

  ws_cfg.uri = ws_uri;
 // ws_cfg.buffer_size = MTU_SIZE;
  ws_cfg.disable_pingpong_discon = true;

  auto client = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                app_websocket_event_handler, (void *)client);

  esp_websocket_client_start(client);

  peer_init();
  g_pc = peer_connection_create(&config);
  peer_connection_oniceconnectionstatechange(g_pc, oniceconnectionstatechange);
  peer_connection_ondatachannel(g_pc, onmessage, onopen, onclose);



  xTaskCreatePinnedToCore(peer_connection_task, "peer_connection", 8192, NULL, 5, &xPcTaskHandle, 1);


  int state = 0;
  while (true) {
    if(eState == PEER_CONNECTION_NEW && state == 1) {
      state=2;

      // Example sending data
      Livekit__SessionDescription r LIVEKIT__SESSION_DESCRIPTION__INIT;
      r.sdp = peer_connection_get_offer(g_pc);
      r.type = "offer";
      size_t size = livekit__session_description__get_packed_size(&r);
      uint8_t *buffer =(uint8_t*) malloc(size);
      livekit__session_description__pack(&r,buffer);

      esp_err_t err = esp_websocket_client_send_bin(
        client,(char*)buffer,
        size,
        portMAX_DELAY); 
      if (err != ESP_OK) {
        printf("Failed to send data: %d\n", (int)(err));
      } else {
        printf("Data sent successfully\n");
      }

      // livekit__session_description__free_unpacked(&r, NULL);
    }
    if (xSemaphoreTake(app_websocket_mutex, portMAX_DELAY) == pdTRUE) {
      if (!app_websocket_packets.empty()) {
        Livekit__SignalResponse* packet = app_websocket_packets.front();

        switch (packet->message_case) {
          case LIVEKIT__SIGNAL_RESPONSE__MESSAGE__NOT_SET:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE__NOT_SET\n");
            break;
          case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_ANSWER:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_ANSWER\n");
            break;
          case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_JOIN:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_JOIN\n");
            state=1;
            break;
          case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_LEAVE:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_LEAVE\n");
            break;
          case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_MUTE:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_MUTE\n");
            break;
          case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_OFFER:
            ESP_LOGI(LOG_TAG, "%s", packet->offer->type);
            ESP_LOGI(LOG_TAG, "%s", packet->offer->sdp);
            peer_connection_set_remote_description(g_pc,packet->offer->sdp);
            
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_OFFER\n");
            break;
          case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_ROOM_UPDATE:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_ROOM_UPDATE\n");
            break;
          case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_SPEAKERS_CHANGED:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_SPEAKERS_CHANGED\n");
            break;
          case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRACK_PUBLISHED:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRACK_PUBLISHED\n");
            break;
          case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRICKLE:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_TRICKLE\n");
            break;
          case LIVEKIT__SIGNAL_RESPONSE__MESSAGE_UPDATE:
            ESP_LOGI(LOG_TAG, "LIVEKIT__SIGNAL_RESPONSE__MESSAGE_UPDATE\n");
            break;
          default:
            ESP_LOGI(LOG_TAG, "Unknown message type received.\n");
            break;
        }
        app_websocket_packets.erase(app_websocket_packets.begin());
        livekit__signal_response__free_unpacked(packet, NULL);
      }
      xSemaphoreGive(app_websocket_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

