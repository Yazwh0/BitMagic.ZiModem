#pragma once

// Matches the subset of Arduino's WiFiClient used by zimodem (verified by grep across
// wificlientnode.h/.ino, proto_http.ino, proto_ftp.ino, zircmode.ino): connect, available,
// read (both no-arg and buffer forms), peek, write (both single-byte and buffer forms),
// setNoDelay, stop, connected, localPort, remoteIP.
//
// Real Arduino's WiFiClient shares its underlying socket via a refcounted internal
// context, which zimodem's own code actually depends on: wificlientnode.ino does
// `clientPtr = createWiFiClient(...); client = *clientPtr; clientPtr->connect(...);` --
// i.e. it assigns through a WiFiClient* first, then mutates the connection through that
// same pointer, and expects the earlier-assigned `client` value to observe the same live
// connection. A shared_ptr<TcpSocket> member (rather than owning a TcpSocket by value)
// is what makes that copy-then-mutate-through-the-original-pointer pattern behave
// correctly instead of silently operating on two independent sockets.

#include <memory>
#include <string>

#include "IPAddress.h"
#include "Stream.h"
#include "zimodem_hal/net.h"

class WiFiClient : public Stream
{
public:
    WiFiClient() : socket_(std::make_shared<zimodem_hal::net::TcpSocket>()) {}

    explicit WiFiClient(zimodem_hal::net::TcpSocket&& socket)
        : socket_(std::make_shared<zimodem_hal::net::TcpSocket>(std::move(socket)))
    {
    }

    int connect(const char* host, uint16_t port) { return socket_->connect(host ? host : "", port) ? 1 : 0; }
    int connect(IPAddress ip, uint16_t port) { return connect(ip.toString().c_str(), port); }

    int connected() { return socket_->connected() ? 1 : 0; }

    int available() override { return socket_->available(); }
    int read() override
    {
        uint8_t b;
        return socket_->read(&b, 1) == 1 ? b : -1;
    }
    int read(uint8_t* buf, size_t size) { return socket_->read(buf, size); }
    int peek() override { return socket_->peek_byte(); }

    size_t write(uint8_t b) override { return socket_->write(&b, 1); }
    size_t write(const uint8_t* buf, size_t size) override { return socket_->write(buf, size); }
    using Print::write;

    void flush() override { socket_->flush(); }
    void setNoDelay(bool nodelay) { socket_->set_no_delay(nodelay); }
    void stop() { socket_->close(); }

    uint16_t localPort() { return socket_->local_port(); }
    IPAddress remoteIP() { return IPAddress(socket_->remote_ip()); }

private:
    std::shared_ptr<zimodem_hal::net::TcpSocket> socket_;
};
