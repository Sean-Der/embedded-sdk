#include "main.h"

#include <esp_event.h>
#include <esp_log.h>
#include <peer.h>

const char *SANTA_SYSTEM_PROMPT =
    R"FOO(You are Santa Claus. Your job is to make kids across the world happy and experience the joy of Christmas.)FOO";
#ifndef LINUX_BUILD
#include "nvs_flash.h"
extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
#else
int main(void) {
#endif
  ESP_LOGI(LOG_TAG, "Starting app with api key: %s", UVAPI_API_KEY);
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  peer_init();
  app_wifi();
  CallRequest request;
  request.system_prompt = SANTA_SYSTEM_PROMPT;
  uv_run(request, UVAPI_API_KEY);
}
