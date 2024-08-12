#ifndef DERIVEDCLASS_H
#define DERIVEDCLASS_H

#include "websocket.hpp"

class Esp32IDFWebSocketClient : public WebSocketClient {
    public:
        Esp32IDFWebSocketClient();
        ~Esp32IDFWebSocketClient();
        int read() override; // Override base class method
        int write() override;
        int connect() override;
};
#endif // DERIVEDCLASS_H
