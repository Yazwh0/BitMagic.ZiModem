#include "zimodem_hal/log.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace zimodem_hal::log;

namespace
{
    struct LogGuard
    {
        ~LogGuard() { set_sink(nullptr); }
    };
}

TEST_CASE("write with no sink registered does not crash", "[log]")
{
    LogGuard guard;
    set_sink(nullptr);
    write("hello");
}

TEST_CASE("write delivers the exact message to the registered sink", "[log]")
{
    LogGuard guard;
    std::vector<std::string> messages;
    set_sink([&](const std::string& m) { messages.push_back(m); });

    write("hello");

    REQUIRE(messages.size() == 1);
    REQUIRE(messages[0] == "hello");
}

TEST_CASE("writef formats like printf before reaching the sink", "[log]")
{
    LogGuard guard;
    std::vector<std::string> messages;
    set_sink([&](const std::string& m) { messages.push_back(m); });

    writef("Baud %d, Deque constant now: %d", 115200, 303);

    REQUIRE(messages.size() == 1);
    REQUIRE(messages[0] == "Baud 115200, Deque constant now: 303");
}

TEST_CASE("zimodem_hal_debug_printf is the global entry point debugPrintf resolves to on the host build", "[log]")
{
    LogGuard guard;
    std::vector<std::string> messages;
    set_sink([&](const std::string& m) { messages.push_back(m); });

    zimodem_hal_debug_printf("Connected to %s with IP %s.\r\n", "myssid", "192.168.1.50");

    REQUIRE(messages.size() == 1);
    REQUIRE(messages[0] == "Connected to myssid with IP 192.168.1.50.\r\n");
}
