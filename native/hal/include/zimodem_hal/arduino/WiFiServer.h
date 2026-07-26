#pragma once

// Matches the subset of Arduino's WiFiServer used by wifiservernode.h/.ino: construct
// from a port, begin(), stop(), close(), hasClient(), available() (accepts and returns
// the pending connection as a WiFiClient, or a closed one if none pending).

#include <cstdint>

#include "WiFiClient.h"
#include "zimodem_hal/net.h"

class WiFiServer
{
public:
    explicit WiFiServer(uint16_t port) : port_(port) {}

    void begin() { listener_.listen(port_); }
    void stop() { listener_.close(); }
    void close() { listener_.close(); }

    bool hasClient() { return listener_.has_pending_client(); }

    WiFiClient available() { return WiFiClient(listener_.accept()); }

private:
    uint16_t port_;
    zimodem_hal::net::TcpListener listener_;
};
