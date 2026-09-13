// Protocol-level test for INCLUDE_SSH (WiFiSSHClient, patches 0013-0017) against a real
// public SSH server. Unlike the IRC/HTTP fake-server tests in test_protocol.cpp, there is
// no cheap way to fake the other end of an SSH connection here -- a compliant peer needs a
// full KEX handshake, packet encryption/MAC, and userauth negotiation before anything else
// happens, so a hand-rolled fake server would effectively be a second SSH implementation.
// This mirrors test_tls.cpp's own [.live] end-to-end case: one minimal, real, network-
// dependent test, not a suite -- hidden from a default offline `ctest` run.

#include "zimodem_hal/fs_root.h"
#include "zimodem_hal/net.h"
#include "zimodem_hal/pins.h"
#include "zimodem_hal/serial_port.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>

// Defined in zimodem.ino, compiled into zimodem_core.
extern void setup();
extern void loop();

namespace
{
    void feed(const std::string& s)
    {
        zimodem_hal::serial::feed_input(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    void drain_into(std::string& out)
    {
        while (zimodem_hal::serial::rx_available())
            out.push_back(static_cast<char>(zimodem_hal::serial::rx_read()));
        zimodem_hal::serial::set_data_ready_callback([&out]() {
            while (zimodem_hal::serial::rx_available())
                out.push_back(static_cast<char>(zimodem_hal::serial::rx_read()));
        });
    }

    template <typename Predicate>
    void pump_more(std::string& collected, Predicate done, int maxIterations = 400)
    {
        drain_into(collected);
        for (int i = 0; i < maxIterations && !done(); i++)
        {
            loop();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    template <typename Predicate>
    std::string pump_until(Predicate done, int maxIterations = 400)
    {
        std::string collected;
        pump_more(collected, done, maxIterations);
        return collected;
    }

    void discard_startup_output()
    {
        std::string discarded;
        drain_into(discarded);
        int quietIterations = 0;
        for (int i = 0; i < 400 && quietIterations < 10; i++)
        {
            size_t before = discarded.size();
            loop();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            quietIterations = (discarded.size() == before) ? quietIterations + 1 : 0;
        }
    }
}

TEST_CASE("AT dial to a phonebook entry with a user:pass@host:port address opens a real SSH session", "[ssh][.live]")
{
    zimodem_hal::serial::reset_for_testing();
    zimodem_hal::pins::reset_for_testing();
    std::string dataDir = zimodem_hal::fs::reset_to_fresh_temp_dir_for_testing();
    zimodem_hal::net::global_init();

    // Written directly rather than through ATP or the AT+irc/AT+config add-entry menus --
    // both have their own independently-confirmed bugs rejecting a multi-colon address
    // like this one (see docs/native-wrapper-spec.md). PhoneBookEntry::loadPhonebook()
    // (phonebook.ino) does no address validation at load time, so this is the correct,
    // direct route to the storage format upstream itself defines
    // (PhoneBookEntry::savePhonebook()'s "%ul,%s,%s,%s\n" -- number,address,modifiers,
    // notes). "s" in the modifiers field is the secure flag (connSettings.ino accepts
    // either case) -- WiFiSSHClient is only selected when FLAG_SECURE is set AND a
    // username is present in the address (wificlientnode.ino's constructNode()).
    {
        std::ofstream f(zimodem_hal::fs::resolve("/zphonebook.txt"), std::ios::binary);
        f << "42,demo:password@test.rebex.net:22,s,ssh live test\n";
    }

    setup();
    discard_startup_output();

    std::string response;
    feed("ATD42\r");
    pump_more(response, [&] {
        return response.find("CONNECT") != std::string::npos ||
               response.find("NO ANSWER") != std::string::npos ||
               response.find("ERROR") != std::string::npos;
    }, 600);

    INFO("response: " << response);
    REQUIRE(response.find("CONNECT") != std::string::npos);
}
