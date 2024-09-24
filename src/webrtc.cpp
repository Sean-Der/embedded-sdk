#include <esp_event.h>
#include <esp_log.h>
#include <pthread.h>
#include <string.h>

#include "main.h"

extern SemaphoreHandle_t g_mutex;

char *offer_buffer = NULL;
char *ice_candidate_buffer = NULL;
char *answer_ice_ufrag = NULL;
char *answer_ice_pwd = NULL;
char *answer_fingerprint = NULL;

PeerConnection *subscriber_peer_connection = NULL;
PeerConnection *publisher_peer_connection = NULL;

static void onconnectionstatechange_task(PeerConnectionState state,
                                         void *user_data) {
    ESP_LOGI(LOG_TAG, "PeerConnectionState: %s",
             peer_connection_state_to_string(state));
}

// on_icecandidate_task holds lock because peer_connection_task is
// what causes it to be fired
static void on_icecandidate_task(char *description, void *user_data) {
    auto fingerprint = strstr(description, "a=fingerprint");
    answer_fingerprint =
        strndup(fingerprint, (int)(strchr(fingerprint, '\r') - fingerprint));

    auto iceUfrag = strstr(description, "a=ice-ufrag");
    answer_ice_ufrag =
        strndup(iceUfrag, (int)(strchr(iceUfrag, '\r') - iceUfrag));

    auto icePwd = strstr(description, "a=ice-pwd");
    answer_ice_pwd = strndup(icePwd, (int)(strchr(icePwd, '\r') - icePwd));
}

void *peer_connection_task(void *user_data) {
    while (1) {
        if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            if (offer_buffer != NULL) {
                auto s = peer_connection_get_state(subscriber_peer_connection);
                if (s == PEER_CONNECTION_COMPLETED ||
                    ice_candidate_buffer != NULL) {
                    if (ice_candidate_buffer != NULL) {
                        peer_connection_add_ice_candidate(
                            subscriber_peer_connection, ice_candidate_buffer);
                    }

                    peer_connection_set_remote_description(
                        subscriber_peer_connection, offer_buffer);

                    free(offer_buffer);
                    offer_buffer = NULL;

                    free(ice_candidate_buffer);
                    ice_candidate_buffer = NULL;
                }
            }

            xSemaphoreGive(g_mutex);
        }

        peer_connection_loop(subscriber_peer_connection);
        peer_connection_loop(publisher_peer_connection);

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    pthread_exit(NULL);
    return NULL;
}

PeerConnection *app_create_peer_connection(int isPublisher) {
    PeerConfiguration peer_connection_config = {
        .ice_servers = {},
        .audio_codec = CODEC_OPUS,
        .video_codec = CODEC_NONE,
        .datachannel = DATA_CHANNEL_STRING,
        .onaudiotrack = [](uint8_t *data, size_t size, void *userdata) -> void {
        },
        .onvideotrack = NULL,
        .on_request_keyframe = NULL,
        .user_data = NULL,
    };

    PeerConnection *peer_connection =
        peer_connection_create(&peer_connection_config);

    peer_connection_oniceconnectionstatechange(peer_connection,
                                               onconnectionstatechange_task);
    if (!isPublisher) {
        peer_connection_onicecandidate(peer_connection, on_icecandidate_task);
    }

    return peer_connection;
}

static const char *sdp_no_audio =
    "v=0\r\n"
    "o=- 8611954123959290783 2 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic:  iot\r\n"
    "a=group:BUNDLE datachannel\r\n"
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=setup:passive\r\n"
    "a=mid:datachannel\r\n"
    "%s\r\n"  // a=ice-ufrag
    "%s\r\n"  // a=ice-pwd
    "%s\r\n"  // a=fingeprint
    "a=sctp-port:5000\r\n";

static const char *sdp_audio =
    "v=0\r\n"
    "o=- 8611954123959290783 2 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic:  iot\r\n"
    "a=group:BUNDLE datachannel audio\r\n"
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=setup:passive\r\n"
    "a=mid:datachannel\r\n"
    "%s\r\n"  // a=ice-ufrag
    "%s\r\n"  // a=ice-pwd
    "%s\r\n"  // a=fingeprint
    "a=sctp-port:5000\r\n"
    "m=audio 9 UDP/TLS/RTP/SAVP 111\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=setup:passive\r\n"
    "a=mid:audio\r\n"
    "%s\r\n"  // a=ice-ufrag
    "%s\r\n"  // a=ice-pwd
    "%s\r\n"  // a=fingeprint
    "a=recvonly\r\n";

void populate_answer(char *answer, int include_audio) {
    if (include_audio) {
        sprintf(answer, sdp_audio, answer_ice_ufrag, answer_ice_pwd,
                answer_fingerprint, answer_ice_ufrag, answer_ice_pwd,
                answer_fingerprint);
    } else {
        sprintf(answer, sdp_no_audio, answer_ice_ufrag, answer_ice_pwd,
                answer_fingerprint);
    }
}
