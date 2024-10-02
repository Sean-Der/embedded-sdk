#include <peer.h>

#include <string>

#define LOG_TAG "embedded-sdk"
#define BUFFER_SAMPLES 320
#define SAMPLE_RATE 8000

struct CallRequest {
  std::string system_prompt;
  std::string voice;
};

void uv_run(const CallRequest &request, const char *api_key);

PeerConnection *lk_create_peer_connection(int isPublisher);
void lk_websocket(const char *url, const char *token);
void lk_wifi(void);
void lk_init_audio_capture(void);
void lk_init_audio_decoder(void);
void lk_populate_answer(char *answer, size_t answer_size, int include_audio);
void lk_publisher_peer_connection_task(void *user_data);
void lk_subscriber_peer_connection_task(void *user_data);
void lk_audio_encoder_task(void *arg);
void lk_audio_decode(uint8_t *data, size_t size);
void lk_init_audio_encoder();
void lk_send_audio(PeerConnection *peer_connection);
