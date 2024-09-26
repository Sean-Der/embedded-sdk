#include <peer.h>

static const char *LOG_TAG = "embedded-sdk";

PeerConnection *lk_create_peer_connection(int isPublisher);
void lk_websocket(void);
void lk_wifi(void);
void lk_populate_answer(char *answer, int include_audio);
void *lk_peer_connection_task(void *user_data);
