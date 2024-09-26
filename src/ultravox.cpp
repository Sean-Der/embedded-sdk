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
    case HTTP_EVENT_ON_DATA:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_ON_DATA, len=%d", event->data_len);
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

static CallResponse create_call(const CallRequest &request,
                                const char *api_key) {
  // format request to json
  cJSON *request_json = cJSON_CreateObject();
  cJSON_AddStringToObject(request_json, "systemPrompt",
                          request.system_prompt.c_str());
  // cJSON_AddStringToObject(request_json, "maxDuration", "00:00:30");
  char *request_str = cJSON_Print(request_json);
  std::string response_str = http_post("/calls", request_str, api_key);
  free(request_str);

  ESP_LOGI(LOG_TAG, "Response: %s", response_str.c_str());
  cJSON *response_json = cJSON_Parse(response_str.c_str());
  if (response_json == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to parse JSON response");
    return CallResponse();
  }

  cJSON *join_url = cJSON_GetObjectItem(response_json, "joinUrl");
  if (join_url == NULL || !cJSON_IsString(join_url)) {
    ESP_LOGE(LOG_TAG, "Bad JSON response");
    return CallResponse();
  }

  CallResponse response;
  response.join_url = join_url->valuestring;
  cJSON_Delete(response_json);
  return response;
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
    ESP_LOGI(LOG_TAG, "Room url: %s", room_url->valuestring);
    ESP_LOGI(LOG_TAG, "Token: %s", token->valuestring);
  } else {
    ESP_LOGE(LOG_TAG, "Invalid room_info JSON.");
#ifndef LINUX_BUILD
    esp_restart();
#endif
    return;
  }

  lk_websocket(room_url->valuestring, token->valuestring);

  cJSON_Delete(room_url);
  cJSON_Delete(token);
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
      if (data->op_code == 0x08 && data->data_len == 2) {
        return;
      }

      uv_handle_websocket_data((const char *)data->data_ptr, data->data_len);
      break;
    }
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGI(LOG_TAG, "WEBSOCKET_EVENT_ERROR");
      break;
  }
}

void uv_run(const CallRequest &callRequest, const char *apiKey) {
  CallResponse call_response = create_call(callRequest, apiKey);
  ESP_LOGI(LOG_TAG, "Call response: %s", call_response.join_url.c_str());

  esp_websocket_client_config_t ws_cfg;
  memset(&ws_cfg, 0, sizeof(ws_cfg));
  ws_cfg.uri = call_response.join_url.c_str();
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
