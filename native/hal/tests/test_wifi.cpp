#include "zimodem_hal/arduino/WiFi.h"
#include "zimodem_hal/arduino/WiFiClient.h"
#include "zimodem_hal/arduino/WiFiServer.h"
#include "zimodem_hal/arduino/WiFiUdp.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

namespace
{
    struct GlobalNetGuard
    {
        GlobalNetGuard() { zimodem_hal::net::global_init(); }
        ~GlobalNetGuard() { zimodem_hal::net::global_shutdown(); }
    };

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

TEST_CASE("WiFi reports always-connected against a placeholder address", "[wifi]")
{
    REQUIRE(WiFi.status() == WL_CONNECTED);
    REQUIRE(std::string(WiFi.localIP().toString().c_str()) != "0.0.0.0");
}

TEST_CASE("WiFi.begin/mode/config/disconnect are accepted no-ops", "[wifi]")
{
    REQUIRE(WiFi.begin("anyssid", "anypassword"));
    WiFi.mode(WIFI_STA);
    REQUIRE(WiFi.config(IPAddress(10, 0, 0, 5), IPAddress(10, 0, 0, 1), IPAddress(255, 255, 255, 0), IPAddress(8, 8, 8, 8)));
    WiFi.disconnect();
    // Still reports connected afterward -- matches the "always connected" host policy.
    REQUIRE(WiFi.status() == WL_CONNECTED);
}

TEST_CASE("WiFi.hostByName resolves a dotted IP directly", "[wifi]")
{
    GlobalNetGuard guard;
    IPAddress out;
    REQUIRE(WiFi.hostByName("127.0.0.1", out));
    REQUIRE(std::string(out.toString().c_str()) == "127.0.0.1");
}

TEST_CASE("WiFi scan stubs report zero networks and a placeholder MAC, matching AT+WIFI on a host with no radio", "[wifi]")
{
    REQUIRE(WiFi.scanNetworks() == 0);
    REQUIRE(std::string(WiFi.macAddress().c_str()) == "00:00:00:00:00:00");
}

TEST_CASE("WiFiClient connects and exchanges data with a WiFiServer, matching ATDT/answer flow", "[wifi]")
{
    GlobalNetGuard guard;
    WiFiServer server(18780);
    server.begin();

    WiFiClient dialer;
    REQUIRE(dialer.connect("127.0.0.1", 18780));

    REQUIRE(wait_until([&] { return server.hasClient(); }));
    WiFiClient accepted = server.available();
    REQUIRE(accepted.connected());

    const uint8_t out[] = {'H', 'I'};
    dialer.write(out, sizeof(out));
    REQUIRE(wait_until([&] { return accepted.available() > 0; }));
    uint8_t buf[8] = {0};
    int n = accepted.read(buf, sizeof(buf));
    REQUIRE(n == 2);
    REQUIRE(std::string(reinterpret_cast<char*>(buf), 2) == "HI");
}

TEST_CASE("copying a WiFiClient shares the same underlying connection (constructNode's client = *clientPtr pattern)", "[wifi]")
{
    GlobalNetGuard guard;
    WiFiServer server(18781);
    server.begin();

    WiFiClient* clientPtr = new WiFiClient();
    WiFiClient client = *clientPtr; // copy BEFORE connecting, like wificlientnode.ino does
    REQUIRE(clientPtr->connect("127.0.0.1", 18781));

    // The connection made through clientPtr must be observable through the earlier copy.
    REQUIRE(wait_until([&] { return client.connected(); }));
    delete clientPtr;
}

TEST_CASE("WiFiClient accepted from a server reports the client's remoteIP and its own localPort", "[wifi]")
{
    GlobalNetGuard guard;
    WiFiServer server(18782);
    server.begin();

    WiFiClient dialer;
    REQUIRE(dialer.connect("127.0.0.1", 18782));
    REQUIRE(wait_until([&] { return server.hasClient(); }));
    WiFiClient accepted = server.available();

    REQUIRE(accepted.localPort() == 18782);
    REQUIRE((accepted.remoteIP()[0] == 127));
}

TEST_CASE("WiFiUDP NTP-style exchange: one socket sends, another parses and reads", "[wifi]")
{
    GlobalNetGuard guard;
    WiFiUDP receiver;
    REQUIRE(receiver.begin(18903));

    WiFiUDP sender;
    REQUIRE(sender.begin(0));
    REQUIRE(sender.beginPacket(IPAddress(127, 0, 0, 1), 18903));
    uint8_t packet[48] = {0};
    packet[0] = 0x1B; // matches rt_clock.ino's NTP request first byte convention
    REQUIRE(sender.write(packet, sizeof(packet)) == sizeof(packet));
    REQUIRE(sender.endPacket());

    int size = 0;
    REQUIRE(wait_until([&] { size = receiver.parsePacket(); return size > 0; }));
    REQUIRE(size == 48);
    uint8_t received[48] = {0};
    REQUIRE(receiver.read(received, sizeof(received)) == 48);
    REQUIRE(received[0] == 0x1B);
}
