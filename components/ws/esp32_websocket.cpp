#include "esp32_websocket.hpp"
// #include "esp_tls.h"
// #include "esp_log.h"
// #include "string.h"
const char *TAG = "HTTP_Client";

int Esp32IDFWebSocketClient::connect()
{
    // esp_tls_cfg_t cfg = {
    //     .cacert_pem_buf = NULL,
    //     // .cacert_pem_bytes = server_cert_pem_end - server_cert_pem_start,
    // };
    // esp_tls_t *tls = esp_tls_init();

    // if (tls == NULL)
    // {
    //     ESP_LOGE(TAG, "Connection failed...");
    //     return 0;
    // }

    // int socket = esp_tls_conn_http_new_sync("embeded-app-4da61xdw.livekit.cloud", &cfg, tls);

    // if (socket != 0)
    // {
    //     ESP_LOGI(TAG, "Failed");
    //     return socket;
    // }

    // ESP_LOGI(TAG, "Connected!");

    return 0;
}

int Esp32IDFWebSocketClient::read()
{
    return 0;
}

int Esp32IDFWebSocketClient::write()
{
    return 0;
}
