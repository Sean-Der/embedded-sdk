// #ifndef LINUX_BUILD
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// #include "esp32_websocket.hpp"

extern void app_wifi(void);
// extern void app_websocket(void);

extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  app_wifi();

  vTaskDelay(pdMS_TO_TICKS(5000));


  // Esp32IDFWebSocketClient client;

  // client.connect();

  // app_websocket();
}
// #else
// extern void app_websocket(void);

// extern "C" void app_main(void) { app_websocket(); }
// #endif
