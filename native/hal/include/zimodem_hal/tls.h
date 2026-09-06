#pragma once

// Internal HAL API (not Arduino-shaped). Backs WiFiClientSecure in
// zimodem_hal/arduino/WiFiClientSecure.h: an outbound TLS client stream layered on top
// of zimodem_hal::net::TcpSocket, using a bundled mbedTLS (fetched + static-linked by
// native/CMakeLists.txt).
//
// Scope -- deliberately narrow, matching the vendored sketch's entire use of it
// (wificlientnode.h::createWiFiClient, routed to the host build by patches/zimodem/0011):
//     WiFiClientSecure *c = new WiFiClientSecure(); c->setInsecure();
//   * outbound client sockets only (no server side);
//   * NO peer certificate / hostname verification -- the sketch never pins a CA or cert
//     and always calls setInsecure(). This encrypts the stream and does SNI, but does
//     not authenticate the peer. Same security posture as the ESP32 firmware build,
//     which also calls setInsecure() unconditionally here.
//   * TLS versions are whatever the bundled mbedTLS offers (see docs/native-wrapper-spec
//     "ESP32 parity"): currently the 2.28 LTS line, i.e. up to TLS 1.2.
//
// Read paths are non-blocking-poll style (available()/read() return immediately, 0 if
// nothing is ready yet, -1 once the session is dead) to match TcpSocket and how zimodem
// drives its clients -- it polls available()/read() from loop(), it never blocks on a
// read. connect() (TCP connect + TLS handshake) is the one blocking call, same as
// TcpSocket::connect(): callers are on the wrapper's dedicated background thread.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "net.h"

namespace zimodem_hal::net
{
    // Defined in tls.cpp. Namespace-scope (not nested) so the file's internal BIO/pump
    // helpers can touch it; keeps the mbedTLS headers out of every translation unit that
    // includes this one (i.e. the whole Arduino sketch, via WiFiClientSecure.h).
    struct TlsSocketImpl;

    class TlsSocket
    {
    public:
        TlsSocket();
        ~TlsSocket();
        TlsSocket(const TlsSocket&) = delete;
        TlsSocket& operator=(const TlsSocket&) = delete;

        // Opens the TCP connection to host:port, then runs the TLS handshake. Blocking,
        // with an internal timeout. Returns false on DNS/TCP failure, handshake failure,
        // or timeout -- in which case the object is left safely closed.
        bool connect(const std::string& host, uint16_t port);

        // True while the handshake has completed and neither a fatal TLS error nor a
        // peer close has been observed and the underlying TCP socket still looks up.
        bool connected() const;

        // Bytes of decrypted application data ready to read right now (pumps one TLS
        // record first if the buffer is empty). 0 if none yet.
        int available();

        // Copies up to len decrypted bytes. Returns bytes copied (0 if none ready yet,
        // -1 once the session is finished/errored) -- mirrors TcpSocket::read().
        int read(uint8_t* buf, size_t len);

        // Next decrypted byte without consuming it, or -1 if none ready.
        int peek_byte();

        // Encrypts and sends up to len bytes. Returns bytes accepted (may be < len if
        // the send side stayed blocked past the internal timeout).
        size_t write(const uint8_t* buf, size_t len);

        void set_no_delay(bool enable);
        void flush();
        void close();

        uint16_t local_port() const;
        uint32_t remote_ip() const;

    private:
        std::unique_ptr<TlsSocketImpl> impl_;
    };
}
