#include <peer.h>

#define LOG_TAG "embedded-sdk"
#define BUFFER_SAMPLES 320

PeerConnection *lk_create_peer_connection(int isPublisher);
void lk_websocket(void);
void lk_wifi(void);
void lk_init_audio_capture(void);
void lk_populate_answer(char *answer, int include_audio);
void lk_peer_connection_task(void *user_data);
void lk_audio_encoder_task(void *arg);
