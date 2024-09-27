#include <peer.h>

#include <string>

#define LOG_TAG "embedded-sdk"
#define BUFFER_SAMPLES 320

struct CallRequest {
  std::string system_prompt;
};

void app_wifi(void);
void lk_init_audio_capture(void);
void uv_run(const CallRequest &request, const char *api_key);
PeerConnection *lk_create_peer_connection(int isPublisher);
void lk_websocket(const char *url, const char *token);
void lk_populate_answer(char *answer, int include_audio);
void lk_publisher_peer_connection_task(void *user_data);
void lk_subscriber_peer_connection_task(void *user_data);
void lk_audio_encoder_task(void *arg);
