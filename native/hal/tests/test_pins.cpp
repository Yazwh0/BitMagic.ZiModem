#include "zimodem_hal/pins.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace zimodem_hal::pins;

namespace
{
    struct PinsGuard
    {
        PinsGuard() { reset_for_testing(); }
        ~PinsGuard() { reset_for_testing(); }
    };
}

TEST_CASE("digital_read defaults to 0 for a pin that has never been written", "[pins]")
{
    PinsGuard guard;
    REQUIRE(digital_read(14) == 0);
}

TEST_CASE("digital_write is observable via digital_read", "[pins]")
{
    PinsGuard guard;
    digital_write(14, 1);
    REQUIRE(digital_read(14) == 1);
    digital_write(14, 0);
    REQUIRE(digital_read(14) == 0);
}

TEST_CASE("digital_write raises the signal callback only on an actual change", "[pins]")
{
    PinsGuard guard;
    std::vector<std::pair<int, int>> events;
    set_signal_callback([&](int pin, int value) { events.emplace_back(pin, value); });

    digital_write(14, 1); // 0 -> 1: change
    digital_write(14, 1); // 1 -> 1: no change, no event
    digital_write(14, 0); // 1 -> 0: change

    REQUIRE(events.size() == 2);
    CHECK(events[0] == std::make_pair(14, 1));
    CHECK(events[1] == std::make_pair(14, 0));
}

TEST_CASE("pins are independent of one another", "[pins]")
{
    PinsGuard guard;
    digital_write(14, 1); // DCD
    digital_write(27, 0); // DTR
    REQUIRE(digital_read(14) == 1);
    REQUIRE(digital_read(27) == 0);
}

TEST_CASE("reset_for_testing clears both pin state and the callback", "[pins]")
{
    PinsGuard guard;
    int call_count = 0;
    set_signal_callback([&](int, int) { ++call_count; });
    digital_write(14, 1);
    REQUIRE(call_count == 1);

    reset_for_testing();

    REQUIRE(digital_read(14) == 0);
    digital_write(14, 1); // would have incremented call_count if the callback survived
    REQUIRE(call_count == 1);
}
