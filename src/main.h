#include <peer.h>

static const char *LOG_TAG = "embedded-sdk";

PeerConnection *app_create_peer_connection();
void app_websocket(void);
void app_wifi(void);
void populate_answer(char *answer, char *ice_ufrag, char *ice_pwd,
                     char *fingerprint, int include_audio);
