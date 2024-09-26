#include <peer.h>
#include <string>

static const char *LOG_TAG = "embedded-sdk";

struct CallRequest
{
    std::string system_prompt;
};

struct CallResponse
{
    std::string join_url;
};

void app_wifi(void);
void uv_run(const CallRequest &request, const std::string &api_key);
PeerConnection *lk_create_peer_connection();
void lk_websocket(const char *url, const char *token);
void lk_populate_answer(char *answer, char *ice_ufrag, char *ice_pwd,
                        char *fingerprint, int include_audio);
