#include "zimodem_hal/net.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

using namespace zimodem_hal::net;

namespace
{
    struct GlobalNetGuard
    {
        GlobalNetGuard() { global_init(); }
        ~GlobalNetGuard() { global_shutdown(); }
    };

    // Loopback sockets are effectively instant, but non-blocking APIs mean a value can
    // legitimately take a poll cycle or two to show up (kernel scheduling, not a bug).
    // A short bounded retry loop is the correct way to assert on it, not a fixed sleep.
    template <typename Predicate>
    bool wait_until(Predicate pred, int timeoutMs = 2000)
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

TEST_CASE("resolve_ipv4 resolves dotted-decimal addresses directly", "[net]")
{
    GlobalNetGuard guard;
    uint32_t addr = 0;
    REQUIRE(resolve_ipv4("127.0.0.1", addr));
    REQUIRE(addr != 0);
}

TEST_CASE("resolve_ipv4 fails cleanly for garbage input", "[net]")
{
    GlobalNetGuard guard;
    uint32_t addr = 0;
    REQUIRE_FALSE(resolve_ipv4("this is not a hostname !!", addr));
}

TEST_CASE("TcpListener + TcpSocket loopback round trip, matching ATDT dial + data flow", "[net]")
{
    GlobalNetGuard guard;
    const uint16_t port = 18765;

    TcpListener listener;
    REQUIRE(listener.listen(port));
    REQUIRE_FALSE(listener.has_pending_client());

    TcpSocket client;
    REQUIRE(client.connect("127.0.0.1", port));

    REQUIRE(wait_until([&] { return listener.has_pending_client(); }));
    TcpSocket server = listener.accept();
    REQUIRE(server.is_open());

    const uint8_t out[] = {'A', 'T', 'D', 'T'};
    REQUIRE(client.write(out, sizeof(out)) == sizeof(out));

    REQUIRE(wait_until([&] { return server.available() > 0; }));
    uint8_t in[16] = {0};
    int n = server.read(in, sizeof(in));
    REQUIRE(n == 4);
    REQUIRE(std::string(reinterpret_cast<char*>(in), 4) == "ATDT");

    const uint8_t reply[] = {'O', 'K'};
    REQUIRE(server.write(reply, sizeof(reply)) == sizeof(reply));
    REQUIRE(wait_until([&] { return client.available() > 0; }));
    uint8_t in2[16] = {0};
    int n2 = client.read(in2, sizeof(in2));
    REQUIRE(n2 == 2);
    REQUIRE(std::string(reinterpret_cast<char*>(in2), 2) == "OK");
}

TEST_CASE("peek_byte does not consume the byte", "[net]")
{
    GlobalNetGuard guard;
    const uint16_t port = 18766;

    TcpListener listener;
    REQUIRE(listener.listen(port));
    TcpSocket client;
    REQUIRE(client.connect("127.0.0.1", port));
    REQUIRE(wait_until([&] { return listener.has_pending_client(); }));
    TcpSocket server = listener.accept();

    const uint8_t out[] = {'X'};
    client.write(out, 1);
    REQUIRE(wait_until([&] { return server.available() > 0; }));

    REQUIRE(server.peek_byte() == 'X');
    REQUIRE(server.peek_byte() == 'X'); // still there
    uint8_t b = 0;
    REQUIRE(server.read(&b, 1) == 1);
    REQUIRE(b == 'X');
}

TEST_CASE("closing the peer eventually makes connected() report false", "[net]")
{
    GlobalNetGuard guard;
    const uint16_t port = 18767;

    TcpListener listener;
    REQUIRE(listener.listen(port));
    TcpSocket client;
    REQUIRE(client.connect("127.0.0.1", port));
    REQUIRE(wait_until([&] { return listener.has_pending_client(); }));
    TcpSocket server = listener.accept();

    REQUIRE(client.connected());
    server.close();

    REQUIRE(wait_until([&] { return !client.connected(); }));
}

TEST_CASE("local_port and remote_ip reflect the connection, matching WiFiClient::localPort/remoteIP usage", "[net]")
{
    GlobalNetGuard guard;
    const uint16_t port = 18769;

    TcpListener listener;
    REQUIRE(listener.listen(port));
    TcpSocket client;
    REQUIRE(client.connect("127.0.0.1", port));
    REQUIRE(wait_until([&] { return listener.has_pending_client(); }));
    TcpSocket server = listener.accept();

    // The server side's local_port is the listening port; its remote_ip is loopback.
    REQUIRE(server.local_port() == port);
    uint32_t remote = server.remote_ip();
    REQUIRE(remote != 0);
    // 127.0.0.1 in network-byte-order-as-raw-uint32 has 127 in the lowest byte.
    REQUIRE((remote & 0xFF) == 127);
}

TEST_CASE("accept() returns a closed socket when nothing is pending", "[net]")
{
    GlobalNetGuard guard;
    const uint16_t port = 18768;
    TcpListener listener;
    REQUIRE(listener.listen(port));

    TcpSocket accepted = listener.accept();
    REQUIRE_FALSE(accepted.is_open());
}

TEST_CASE("UdpSocket loopback: begin/write/end on one socket, parsePacket/read on another, matching rt_clock.ino's NTP exchange", "[net]")
{
    GlobalNetGuard guard;
    UdpSocket receiver;
    REQUIRE(receiver.begin(18901));

    UdpSocket sender;
    REQUIRE(sender.begin(0));

    REQUIRE(sender.begin_packet("127.0.0.1", 18901));
    const uint8_t payload[] = {1, 2, 3, 4, 5};
    REQUIRE(sender.write(payload, sizeof(payload)) == sizeof(payload));
    REQUIRE(sender.end_packet());

    int size = 0;
    REQUIRE(wait_until([&] { size = receiver.parse_packet(); return size > 0; }));
    REQUIRE(size == 5);

    uint8_t buf[5] = {0};
    REQUIRE(receiver.read(buf, sizeof(buf)) == 5);
    for (int i = 0; i < 5; i++)
        REQUIRE(buf[i] == i + 1);
}

TEST_CASE("UdpSocket.parsePacket returns 0 when nothing has arrived", "[net]")
{
    GlobalNetGuard guard;
    UdpSocket receiver;
    REQUIRE(receiver.begin(18902));
    REQUIRE(receiver.parse_packet() == 0);
}
