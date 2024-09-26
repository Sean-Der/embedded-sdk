#include <esp_event.h>
#include <esp_log.h>
#include <pthread.h>
#include <string.h>

#include "main.h"

extern SemaphoreHandle_t g_mutex;

char *subscriber_offer_buffer = NULL;
char *ice_candidate_buffer = NULL;

// Subscriber answer is generated manually. These are the extracted values
// used to generate the synthetic answer
char *subscriber_answer_ice_ufrag = NULL;
char *subscriber_answer_ice_pwd = NULL;
char *subscriber_answer_fingerprint = NULL;

// publisher_status is a FSM of the following states
// * 0 - NoOp
// * 1 - Send AddTrackRequest
// * 2 - Create Local Offer
// * 3 - Send Local Offer
// * 4 - Handle remote Answer
int publisher_status = 0;
char *publisher_signaling_buffer = NULL;

PeerConnection *subscriber_peer_connection = NULL;
PeerConnection *publisher_peer_connection = NULL;

static void publisher_onconnectionstatechange_task(PeerConnectionState state,
                                                   void *user_data) {
  ESP_LOGI(LOG_TAG, "Publisher PeerConnectionState: %s",
           peer_connection_state_to_string(state));
}

static void subscriber_onconnectionstatechange_task(PeerConnectionState state,
                                                    void *user_data) {
  ESP_LOGI(LOG_TAG, "Subscriber PeerConnectionState: %s",
           peer_connection_state_to_string(state));

  // Subscriber has connected, start connecting publisher
  if (state == PEER_CONNECTION_COMPLETED) {
    publisher_status = 1;
  }
}

// subscriber_on_icecandidate_task holds lock because peer_connection_task is
// what causes it to be fired
static void subscriber_on_icecandidate_task(char *description,
                                            void *user_data) {
  auto fingerprint = strstr(description, "a=fingerprint");
  subscriber_answer_fingerprint =
      strndup(fingerprint, (int)(strchr(fingerprint, '\r') - fingerprint));

  auto iceUfrag = strstr(description, "a=ice-ufrag");
  subscriber_answer_ice_ufrag =
      strndup(iceUfrag, (int)(strchr(iceUfrag, '\r') - iceUfrag));

  auto icePwd = strstr(description, "a=ice-pwd");
  subscriber_answer_ice_pwd =
      strndup(icePwd, (int)(strchr(icePwd, '\r') - icePwd));
}

static void publisher_on_icecandidate_task(char *description, void *user_data) {
  publisher_signaling_buffer = strdup(description);
  publisher_status = 3;
}

// Given a Remote Description + ICE Candidate do a Set+Free on a PeerConnection
void process_signaling_values(PeerConnection *peer_connection,
                              char **ice_candidate, char **remote_description) {
  // If PeerConnection hasn't gone to completed we need a ICECandidate and
  // RemoteDescription libpeer doesn't support Trickle ICE
  auto state = peer_connection_get_state(peer_connection);
  if (state != PEER_CONNECTION_COMPLETED && *ice_candidate == NULL) {
    return;
  }

  // Only call add_ice_candidate when not completed. Calling it on a connected
  // PeerConnection will break it
  if (state != PEER_CONNECTION_COMPLETED && *ice_candidate != NULL) {
    peer_connection_add_ice_candidate(peer_connection, *ice_candidate);
    free(*ice_candidate);
    *ice_candidate = NULL;
  }

  if (*remote_description != NULL) {
    peer_connection_set_remote_description(peer_connection,
                                           *remote_description);
    free(*remote_description);
    *remote_description = NULL;
  }
}

void *peer_connection_task(void *user_data) {
  while (1) {
    if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
      if (publisher_status == 2) {
        peer_connection_create_offer(publisher_peer_connection);
        publisher_status = 0;
      } else if (publisher_status == 4) {
        process_signaling_values(publisher_peer_connection,
                                 &ice_candidate_buffer,
                                 &publisher_signaling_buffer);
      }

      process_signaling_values(subscriber_peer_connection,
                               &ice_candidate_buffer, &subscriber_offer_buffer);

      xSemaphoreGive(g_mutex);
    }

    peer_connection_loop(subscriber_peer_connection);
    peer_connection_loop(publisher_peer_connection);

    vTaskDelay(pdMS_TO_TICKS(1));
  }

  pthread_exit(NULL);
  return NULL;
}

PeerConnection *lk_create_peer_connection(int isPublisher) {
  PeerConfiguration peer_connection_config = {
      .ice_servers = {},
      .audio_codec = CODEC_OPUS,
      .video_codec = CODEC_NONE,
      .datachannel = DATA_CHANNEL_STRING,
      .onaudiotrack = [](uint8_t *data, size_t size, void *userdata) -> void {},
      .onvideotrack = NULL,
      .on_request_keyframe = NULL,
      .user_data = NULL,
  };

  PeerConnection *peer_connection =
      peer_connection_create(&peer_connection_config);
  if (peer_connection == NULL) {
    ESP_LOGE(LOG_TAG, "Failed to create peer connection");
    return NULL;
  }

  if (isPublisher) {
    peer_connection_oniceconnectionstatechange(
        peer_connection, publisher_onconnectionstatechange_task);
    peer_connection_onicecandidate(peer_connection,
                                   publisher_on_icecandidate_task);
  } else {
    peer_connection_oniceconnectionstatechange(
        peer_connection, subscriber_onconnectionstatechange_task);
    peer_connection_onicecandidate(peer_connection,
                                   subscriber_on_icecandidate_task);
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

void lk_populate_answer(char *answer, int include_audio) {
  if (include_audio) {
    sprintf(answer, sdp_audio, subscriber_answer_ice_ufrag,
            subscriber_answer_ice_pwd, subscriber_answer_fingerprint,
            subscriber_answer_ice_ufrag, subscriber_answer_ice_pwd,
            subscriber_answer_fingerprint);
  } else {
    sprintf(answer, sdp_no_audio, subscriber_answer_ice_ufrag,
            subscriber_answer_ice_pwd, subscriber_answer_fingerprint);
  }
}
