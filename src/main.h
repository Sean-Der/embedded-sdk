#include <peer.h>

#define LOG_TAG "embedded-sdk"
#define BUFFER_SAMPLES 320
#define SAMPLE_RATE 8000

PeerConnection *lk_create_peer_connection(int isPublisher);
void lk_websocket(void);
void lk_wifi(void);
void lk_init_audio_capture(void);
void lk_init_audio_decoder(void);
void lk_populate_answer(char *answer, int include_audio);
void lk_publisher_peer_connection_task(void *user_data);
void lk_subscriber_peer_connection_task(void *user_data);
void lk_audio_encoder_task(void *arg);
void lk_audio_decode(uint8_t *data, size_t size);
