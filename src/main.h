#include <peer.h>

static const char *LOG_TAG = "embedded-sdk";

PeerConnection *app_create_peer_connection(int isPublisher);
void app_websocket(void);
void app_wifi(void);
void populate_answer(char *answer, int include_audio);
void *peer_connection_task(void *user_data);
