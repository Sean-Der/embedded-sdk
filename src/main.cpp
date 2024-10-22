#include "main.h"

#include <esp_event.h>
#include <esp_log.h>
#include <peer.h>

#ifndef LINUX_BUILD
#include <esp_h264_enc_single_sw.h>

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
  peer_init();

#if SEND_AUDIO
  lk_init_audio_capture();
  lk_init_audio_decoder();
#endif

#ifdef SEND_VIDEO
  if (lk_init_video_capture() != ESP_OK) {
    printf("Camera Init Failed\n");
    return;
  }

  if (lk_init_video_encoder() != ESP_H264_ERR_OK) {
    printf("Video Encoder failed to start\n");
    return;
  }
#endif

  lk_wifi();
  lk_websocket(LIVEKIT_URL, LIVEKIT_TOKEN);
}
#else
int main(void) {
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  peer_init();
  lk_websocket(LIVEKIT_URL, LIVEKIT_TOKEN);
}
#endif
