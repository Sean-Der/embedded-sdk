#include <peer.h>

#include <string>

static const char *LOG_TAG = "embedded-sdk";

struct CallRequest {
  std::string system_prompt;
};

void app_wifi(void);
void uv_run(const CallRequest &request, const char *api_key);
PeerConnection *lk_create_peer_connection(int isPublisher);
void lk_websocket(const char *url, const char *token);
void lk_populate_answer(char *answer, int include_audio);
void *lk_peer_connection_task(void *user_data);
