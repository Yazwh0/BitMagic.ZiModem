// First real behavioral tests of the compiled sketch: calls the actual setup()/loop()
// from zimodem_core (not a mock, not a stub -- the genuine vendored+patched code) and
// drives it the same way the eventual C ABI wrapper will: feed bytes in via
// zimodem_hal::serial::feed_input, observe bytes out via the output callback.
//
// NOTE: rt_clock.ino's clock tick periodically fires a real NTP request over a real UDP
// socket to a real public time server (zimodem_hal::net has no fake/offline NTP
// responder yet). That's a genuine external-network dependency in this test binary --
// harmless so far (doesn't affect these assertions, tests still run in a couple of
// seconds), but worth revisiting before this suite runs somewhere without internet
// access or as part of a stricter hermetic-test policy.

#include "zimodem_hal/fs_root.h"
#include "zimodem_hal/net.h"
#include "zimodem_hal/pins.h"
#include "zimodem_hal/serial_port.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

// Defined in zimodem.ino, compiled into zimodem_core.
extern void setup();
extern void loop();

namespace
{
    struct CoreTestGuard
    {
        CoreTestGuard()
        {
            zimodem_hal::serial::reset_for_testing();
            zimodem_hal::pins::reset_for_testing();
            zimodem_hal::fs::reset_to_fresh_temp_dir_for_testing();
            zimodem_hal::net::global_init();
        }
        ~CoreTestGuard() { zimodem_hal::net::global_shutdown(); }
    };

    std::string capture_output_over(int loop_iterations)
    {
        std::string out;
        zimodem_hal::serial::set_output_callback([&](const uint8_t* data, size_t len) {
            out.append(reinterpret_cast<const char*>(data), len);
        });
        for (int i = 0; i < loop_iterations; i++)
            loop();
        return out;
    }

    // Matches how a real terminal driving the modem would poll: keep pumping loop() and
    // draining serial output until either `done` is satisfied or a generous bound is hit.
    template <typename Predicate>
    std::string pump_until(Predicate done, int maxIterations = 400)
    {
        std::string collected;
        zimodem_hal::serial::set_output_callback([&](const uint8_t* data, size_t len) {
            collected.append(reinterpret_cast<const char*>(data), len);
        });
        for (int i = 0; i < maxIterations && !done(); i++)
        {
            loop();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return collected;
    }

    // DEFAULT_PIN_DCD in our ZIMODEM_HOST platform branch (patches/zimodem/0001).
    constexpr int kDcdPin = 10;
}

TEST_CASE("setup() runs without crashing and loop() can be pumped", "[smoke]")
{
    CoreTestGuard guard;
    setup();
    for (int i = 0; i < 5; i++)
        loop();
    SUCCEED("setup()/loop() completed without crashing");
}

TEST_CASE("plain AT command echoes and replies OK", "[smoke]")
{
    CoreTestGuard guard;
    setup();
    capture_output_over(3); // let any startup banner flush out and discard it

    const uint8_t cmd[] = {'A', 'T', '\r'};
    zimodem_hal::serial::feed_input(cmd, sizeof(cmd));

    std::string response = capture_output_over(5);
    REQUIRE(response == "AT\r\nOK\r\n");
}

TEST_CASE("ATDT dials out over a real TCP socket, gets CONNECT, and DCD asserts (active-low)", "[smoke]")
{
    CoreTestGuard guard;
    setup();
    capture_output_over(3);

    zimodem_hal::net::TcpListener listener;
    REQUIRE(listener.listen(19201));
    REQUIRE(zimodem_hal::pins::digital_read(kDcdPin) != 0); // inactive (HIGH) before dialing

    std::string cmd = "ATDT127.0.0.1:19201\r";
    zimodem_hal::serial::feed_input(reinterpret_cast<const uint8_t*>(cmd.data()), cmd.size());

    std::string response = pump_until([&] { return listener.has_pending_client(); });
    REQUIRE(response == "ATDT127.0.0.1:19201\r\nCONNECT 1\r\n");
    REQUIRE(zimodem_hal::pins::digital_read(kDcdPin) == 0); // active (LOW): connection is up

    zimodem_hal::net::TcpSocket serverSide = listener.accept();
    REQUIRE(serverSide.is_open());
}

TEST_CASE("after CONNECT, bytes typed at the terminal reach the socket and vice versa", "[smoke]")
{
    CoreTestGuard guard;
    setup();
    capture_output_over(3);

    zimodem_hal::net::TcpListener listener;
    REQUIRE(listener.listen(19202));

    std::string cmd = "ATDT127.0.0.1:19202\r";
    zimodem_hal::serial::feed_input(reinterpret_cast<const uint8_t*>(cmd.data()), cmd.size());
    pump_until([&] { return listener.has_pending_client(); });
    zimodem_hal::net::TcpSocket serverSide = listener.accept();
    REQUIRE(serverSide.is_open());

    // Terminal -> socket: bytes fed into the virtual UART while connected should arrive
    // at the far end of the TCP connection.
    const uint8_t typed[] = {'H', 'E', 'L', 'L', 'O'};
    zimodem_hal::serial::feed_input(typed, sizeof(typed));
    bool serverGotIt = false;
    pump_until([&] {
        serverGotIt = serverSide.available() >= 5;
        return serverGotIt;
    });
    REQUIRE(serverGotIt);
    uint8_t buf[5] = {0};
    serverSide.read(buf, 5);
    REQUIRE(std::string(reinterpret_cast<char*>(buf), 5) == "HELLO");

    // Socket -> terminal: bytes the far end sends should come back out through the
    // virtual UART's output callback.
    const uint8_t fromRemote[] = {'W', 'O', 'R', 'L', 'D'};
    serverSide.write(fromRemote, sizeof(fromRemote));
    std::string echoed = pump_until([&] { return false; }, 50); // pump a bounded number of times
    REQUIRE(echoed.find("WORLD") != std::string::npos);
}
