#include "esp_event.h"
#include "esp_log.h"
#include "esp_websocket_client.h"

static const char *LOG_TAG = "embedded-sdk";
#define WEBSOCKET_URI_SIZE 1500

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
      ESP_LOGI(LOG_TAG, "Received opcode=%d", data->op_code);
      if (data->op_code == 0x08 && data->data_len == 2) {
        ESP_LOGW(LOG_TAG, "Received closed message with code=%d",
                 256 * data->data_ptr[0] + data->data_ptr[1]);
      } else {
        ESP_LOGW(LOG_TAG, "Received=%.*s", data->data_len,
                 (char *)data->data_ptr);
      }
      break;
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGI(LOG_TAG, "WEBSOCKET_EVENT_ERROR");
      break;
  }
}

void app_websocket(void) {
  char ws_uri[WEBSOCKET_URI_SIZE];
  snprintf(ws_uri, sizeof(ws_uri), "%s/rtc?access_token=%s", LIVEKIT_URL,
           LIVEKIT_TOKEN);

  esp_websocket_client_config_t ws_cfg;
  memset(&ws_cfg, 0, sizeof(ws_cfg));

  ws_cfg.uri = ws_uri;

  auto client = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                app_websocket_event_handler, (void *)client);

  esp_websocket_client_start(client);

  // TODO. State machine
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

