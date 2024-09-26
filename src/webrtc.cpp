#include <esp_event.h>
#include <esp_log.h>
#include <pthread.h>
#include <string.h>

#include "main.h"

static void onconnectionstatechange_task(PeerConnectionState state,
                                         void *user_data) {
  ESP_LOGI(LOG_TAG, "PeerConnectionState: %s",
           peer_connection_state_to_string(state));
}

PeerConnection *lk_create_peer_connection() {
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

  peer_connection_oniceconnectionstatechange(peer_connection,
                                             onconnectionstatechange_task);

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

void lk_populate_answer(char *answer, char *ice_ufrag, char *ice_pwd,
                        char *fingerprint, int include_audio) {
  if (include_audio) {
    sprintf(answer, sdp_audio, ice_ufrag, ice_pwd, fingerprint, ice_ufrag,
            ice_pwd, fingerprint);
  } else {
    sprintf(answer, sdp_no_audio, ice_ufrag, ice_pwd, fingerprint);
  }
}
