#include <peer.h>

static const char *LOG_TAG = "embedded-sdk";

PeerConnection *app_create_peer_connection();
void *peer_connection_task(void *user_data);
void app_websocket(void);
void app_wifi(void);
