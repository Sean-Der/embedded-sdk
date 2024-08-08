#include "esp_event.h"

extern void app_wifi(void);
extern void app_websocket(void);

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

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  app_wifi();
  app_websocket();
}
#else
int main(void) {
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  app_websocket();
}
#endif
