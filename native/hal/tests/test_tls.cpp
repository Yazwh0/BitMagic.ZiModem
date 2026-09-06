#include "zimodem_hal/tls.h"

#include "WiFiClientSecure.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <string>
#include <thread>

using namespace zimodem_hal::net;

namespace
{
    struct GlobalNetGuard
    {
        GlobalNetGuard() { global_init(); }
        ~GlobalNetGuard() { global_shutdown(); }
    };

    template <typename Predicate>
    bool wait_until(Predicate pred, int timeoutMs = 3000)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (pred())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }
}

// The offline tests below don't do a real handshake (no server-side TLS / cert here) --
// they exercise the wiring: BIO glue over TcpSocket, the blocking handshake loop's error
// and timeout exits, and clean teardown. A genuine end-to-end handshake is the [.live]
// test at the bottom, hidden by default because it needs outbound network.

TEST_CASE("TlsSocket::connect fails fast when nothing is listening", "[tls]")
{
    GlobalNetGuard net;
    TlsSocket tls;
    REQUIRE_FALSE(tls.connect("127.0.0.1", 18789));
    REQUIRE_FALSE(tls.connected());
}

TEST_CASE("TlsSocket::connect fails cleanly against a non-TLS peer", "[tls]")
{
    GlobalNetGuard net;
    const uint16_t port = 18781;

    TcpListener listener;
    REQUIRE(listener.listen(port));

    // Run the (blocking) TLS connect on another thread; meanwhile accept the TCP
    // connection and drop it, so the handshake read hits a closed peer.
    auto fut = std::async(std::launch::async, [&] {
        TlsSocket tls;
        bool ok = tls.connect("127.0.0.1", port);
        return ok || tls.connected();
    });

    REQUIRE(wait_until([&] { return listener.has_pending_client(); }));
    {
        TcpSocket server = listener.accept();
        // brief pause so the client has sent its ClientHello before we close
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        server.close();
    }

    REQUIRE(fut.wait_for(std::chrono::seconds(20)) == std::future_status::ready);
    REQUIRE_FALSE(fut.get());
}

TEST_CASE("WiFiClientSecure basic lifecycle is safe", "[tls]")
{
    GlobalNetGuard net;
    WiFiClientSecure c;
    c.setInsecure();
    REQUIRE(c.connected() == 0);
    REQUIRE(c.available() == 0);
    REQUIRE(c.read() == -1);
    REQUIRE(c.connect("127.0.0.1", 18789) == 0); // no listener
    REQUIRE(c.connected() == 0);
    c.stop(); // must not crash on a never-connected client
}

TEST_CASE("WiFiClientSecure is usable through a WiFiClient*", "[tls]")
{
    GlobalNetGuard net;
    WiFiClient* c = new WiFiClientSecure();
    REQUIRE(c->connected() == 0);
    REQUIRE(c->connect("127.0.0.1", 18789) == 0); // virtual dispatch -> TlsSocket path
    c->stop();
    delete c; // virtual dtor via Print
}

TEST_CASE("TlsSocket end-to-end handshake against a public host", "[tls][.live]")
{
    GlobalNetGuard net;
    TlsSocket tls;
    if (!tls.connect("example.com", 443))
        SKIP("no outbound TLS connectivity to example.com:443");

    REQUIRE(tls.connected());

    const std::string req =
        "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    REQUIRE(tls.write(reinterpret_cast<const uint8_t*>(req.data()), req.size()) == req.size());

    std::string resp;
    wait_until(
        [&] {
            uint8_t buf[512];
            int n = tls.read(buf, sizeof(buf));
            if (n > 0)
                resp.append(reinterpret_cast<char*>(buf), n);
            return resp.find("HTTP/1") != std::string::npos;
        },
        10000);

    REQUIRE(resp.find("HTTP/1") != std::string::npos);
}
