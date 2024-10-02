#include <cJSON.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>

#include <memory>

#include "main.h"

#define MTU_SIZE 1500
#define ULTRAVOX_API_URL "https://api.ultravox.ai/api"

struct HttpResponseData {
  ~HttpResponseData() {
    delete[] buffer;
  }
  const char *data() const {
    return buffer ? buffer : "";
  }
  void allocate(size_t size) {
    delete[] buffer;
    buffer = new char[size];
    buffer_size = size;
    read = 0;
  }
  char *buffer = nullptr;
  size_t buffer_size = 0;
  size_t read = 0;
};

static esp_err_t http_event_handler(esp_http_client_event_t *event) {
  switch (event->event_id) {
    case HTTP_EVENT_ERROR:
      ESP_LOGE(LOG_TAG, "HTTP_EVENT_ERROR");
      break;
    case HTTP_EVENT_ON_CONNECTED:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_ON_CONNECTED");
      break;
    case HTTP_EVENT_HEADER_SENT:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_HEADER_SENT");
      break;
    case HTTP_EVENT_ON_HEADER:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ON_HEADER");
      break;
    case HTTP_EVENT_ON_DATA:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ON_DATA, len=%d", event->data_len);
      {
        auto *data = static_cast<HttpResponseData *>(event->user_data);
        if (data->buffer == nullptr) {
          int content_len = esp_http_client_get_content_length(event->client);
          data->buffer = new char[content_len];
          data->buffer_size = content_len;
        }

        int copy_len = MIN(event->data_len, data->buffer_size - data->read);
        if (copy_len > 0) {
          memcpy(data->buffer + data->read, event->data, copy_len);
          data->read += copy_len;
        }
      }
      break;
    case HTTP_EVENT_ON_FINISH:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_ON_FINISH");
      break;
    case HTTP_EVENT_DISCONNECTED:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_DISCONNECTED");
      break;
    default:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT %d", event->event_id);
      break;
  }
  return ESP_OK;
}

static std::string http_post(const char *path, const char *request_json,
                             const char *api_key) {
  std::unique_ptr<HttpResponseData> data(new HttpResponseData());
  std::string url = ULTRAVOX_API_URL;
  url += path;
  esp_http_client_config_t config;
  memset(&config, 0, sizeof(config));
  config.url = url.c_str();
  config.event_handler = http_event_handler;
  config.user_data = data.get();
  esp_http_client_handle_t client = esp_http_client_init(&config);

  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "x-api-key", api_key);
  esp_http_client_set_post_field(client, request_json, strlen(request_json));

  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    ESP_LOGI(LOG_TAG, "HTTP POST Status = %d, content_length = %lld",
             esp_http_client_get_status_code(client),
             esp_http_client_get_content_length(client));
  } else {
    ESP_LOGE(LOG_TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);
  return std::string(data->data(), data->read);
}

static std::string create_call(const CallRequest &request,
                               const char *api_key) {
  auto request_json = cJSON_CreateObject();
  cJSON_AddStringToObject(request_json, "systemPrompt",
                          request.system_prompt.c_str());
  cJSON_AddStringToObject(request_json, "voice", request.voice.c_str());
  // cJSON_AddStringToObject(request_json, "maxDuration", "00:00:30");
  auto request_str = cJSON_Print(request_json);
  std::string response_str = http_post("/calls", request_str, api_key);
  free(request_str);

  ESP_LOGI(LOG_TAG, "Response: %s", response_str.c_str());
  auto response_json = cJSON_Parse(response_str.c_str());
  if (response_json == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to parse JSON response");
    return "";
  }

  auto join_url = cJSON_GetObjectItem(response_json, "joinUrl");
  if (join_url == NULL || !cJSON_IsString(join_url)) {
    ESP_LOGE(LOG_TAG, "Bad JSON response");
    return "";
  }

  std::string join_url_str = join_url->valuestring;
  cJSON_Delete(response_json);
  return join_url_str;
}

struct LiveKitTaskParams {
  std::string room_url;
  std::string token;
};

static void lk_websocket_thunk(void *pvParameters) {
  auto params = static_cast<LiveKitTaskParams *>(pvParameters);
  lk_websocket(params->room_url.c_str(), params->token.c_str());
  delete params;
}

static void uv_handle_websocket_data(const char *data, size_t len) {
  std::string data_str(data, len);
  ESP_LOGI(LOG_TAG, "Received data: %s", data_str.c_str());
  auto room_info = cJSON_Parse(data_str.c_str());
  if (room_info == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to parse room_info JSON.");
#ifndef LINUX_BUILD
    esp_restart();
#endif
    return;
  }

  auto room_url = cJSON_GetObjectItem(room_info, "roomUrl");
  auto token = cJSON_GetObjectItem(room_info, "token");
  if (room_url != NULL && cJSON_IsString(room_url) && token != NULL &&
      cJSON_IsString(token)) {
    ESP_LOGD(LOG_TAG, "Room url: %s", room_url->valuestring);
    ESP_LOGD(LOG_TAG, "Token: %s", token->valuestring);
  } else {
    ESP_LOGE(LOG_TAG, "Invalid room_info JSON.");
#ifndef LINUX_BUILD
    esp_restart();
#endif
    return;
  }

  LiveKitTaskParams *params = new LiveKitTaskParams();
  params->room_url = room_url->valuestring;
  params->token = token->valuestring;
  xTaskCreate(lk_websocket_thunk, "lk_websocket", 4096, params, 5, NULL);
  cJSON_Delete(room_info);
}

static void uv_websocket_event_handler(void *handler_args,
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
      if (data->op_code == 0x01 && data->data_len > 0) {
        uv_handle_websocket_data((const char *)data->data_ptr, data->data_len);
      }
      break;
    }
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGI(LOG_TAG, "WEBSOCKET_EVENT_ERROR");
      break;
  }
}

void uv_run(const CallRequest &callRequest, const char *apiKey) {
  auto join_url = create_call(callRequest, apiKey);
  ESP_LOGI(LOG_TAG, "Call response: %s", join_url.c_str());

  esp_websocket_client_config_t ws_cfg;
  memset(&ws_cfg, 0, sizeof(ws_cfg));
  ws_cfg.uri = join_url.c_str();
  ws_cfg.buffer_size = MTU_SIZE;
  ws_cfg.disable_pingpong_discon = true;
  ws_cfg.reconnect_timeout_ms = 1000;
  ws_cfg.network_timeout_ms = 1000;

  auto client = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY,
                                uv_websocket_event_handler, (void *)client);
  esp_websocket_client_start(client);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
