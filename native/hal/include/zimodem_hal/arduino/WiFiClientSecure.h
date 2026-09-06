#pragma once

// Arduino-shaped TLS client. The vendored sketch's only use of it is in
// wificlientnode.h::createWiFiClient() -- brought into the ZIMODEM_HOST build by
// patches/zimodem/0011, reusing upstream's own ESP32 code path verbatim:
//
//     WiFiClientSecure *c = new WiFiClientSecure();
//     c->setInsecure();
//     ... then driven through a WiFiClient* (WiFiClientNode::clientPtr) ...
//
// So every method WiFiClientNode calls through that base pointer -- connect / connected
// / available / read / peek / write / flush / stop / setNoDelay / localPort / remoteIP
// -- must be virtual on WiFiClient (it is: see WiFiClient.h) and overridden here to go
// through the TLS transport instead of the plain TcpSocket the base holds.
//
// setInsecure() is the only TLS-config setter the sketch calls. The HAL TlsSocket is
// unconditionally non-verifying (zimodem_hal/tls.h explains why), so setInsecure() only
// records intent. setCACert/setCertificate/setPrivateKey are accepted as no-ops so a
// future upstream sync that starts calling them still compiles -- they would need real
// work in TlsSocket to actually take effect.
//
// NOTE on the base subobject: WiFiClientNode does `client = *clientPtr;` (slicing copy
// into a by-value WiFiClient member) but then only ever uses `clientPtr` for a dialled
// connection -- `client` is touched solely when `clientPtr == null`, which is the
// server-accept path and never secure. So the sliced-away TLS state is harmless here.
// This matches how the real ESP32 WiFiClientSecure (also a WiFiClient subclass) behaves.

#include <memory>

#include "WiFiClient.h"
#include "zimodem_hal/tls.h"

class WiFiClientSecure : public WiFiClient
{
public:
    WiFiClientSecure() : tls_(std::make_shared<zimodem_hal::net::TlsSocket>()) {}

    int connect(const char* host, uint16_t port) override
    {
        return tls_->connect(host ? host : "", port) ? 1 : 0;
    }
    int connect(IPAddress ip, uint16_t port) override { return connect(ip.toString().c_str(), port); }

    int connected() override { return tls_->connected() ? 1 : 0; }

    int available() override { return tls_->available(); }
    int read() override
    {
        uint8_t b;
        return tls_->read(&b, 1) == 1 ? b : -1;
    }
    int read(uint8_t* buf, size_t size) override { return tls_->read(buf, size); }
    int peek() override { return tls_->peek_byte(); }

    size_t write(uint8_t b) override { return tls_->write(&b, 1); }
    size_t write(const uint8_t* buf, size_t size) override { return tls_->write(buf, size); }
    using Print::write;

    void flush() override { tls_->flush(); }
    void setNoDelay(bool nodelay) override { tls_->set_no_delay(nodelay); }
    void stop() override { tls_->close(); }

    uint16_t localPort() override { return tls_->local_port(); }
    IPAddress remoteIP() override { return IPAddress(tls_->remote_ip()); }

    // TLS config surface (see file header).
    void setInsecure() { insecure_ = true; }
    void setCACert(const char* /*pem*/) {}
    void setCACertBundle(const uint8_t* /*bundle*/) {}
    void setCertificate(const char* /*pem*/) {}
    void setPrivateKey(const char* /*pem*/) {}
    bool verify(const char* /*fingerprint*/, const char* /*host*/) { return true; }

private:
    std::shared_ptr<zimodem_hal::net::TlsSocket> tls_;
    bool insecure_ = false;
};
