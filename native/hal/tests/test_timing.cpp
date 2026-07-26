#include "zimodem_hal/timing.h"

#include <catch2/catch_test_macros.hpp>

using namespace zimodem_hal::timing;

namespace
{
    // RAII guard so a failing assertion doesn't leave the fake clock installed for
    // whichever test happens to run next (tests share the HAL's process-global clock).
    struct ClockGuard
    {
        ~ClockGuard() { set_clock_for_testing(nullptr, nullptr); }
    };
}

TEST_CASE("now_ms reflects a real, advancing wall clock by default", "[timing]")
{
    ClockGuard guard;
    auto t1 = now_ms();
    sleep_ms(5);
    auto t2 = now_ms();
    REQUIRE(t2 >= t1);
}

TEST_CASE("set_clock_for_testing overrides now_ms with a deterministic fake clock", "[timing]")
{
    ClockGuard guard;
    std::uint64_t fake_time = 1000;
    set_clock_for_testing([&] { return fake_time; }, nullptr);

    REQUIRE(now_ms() == 1000);
    fake_time = 2000;
    REQUIRE(now_ms() == 2000);
}

TEST_CASE("sleep_ms can be overridden to fast-forward a fake clock instead of blocking", "[timing]")
{
    ClockGuard guard;
    std::uint64_t fake_time = 0;
    set_clock_for_testing(
        [&] { return fake_time; },
        [&](std::uint32_t ms) { fake_time += ms; });

    REQUIRE(now_ms() == 0);
    sleep_ms(900);
    REQUIRE(now_ms() == 900);
}

TEST_CASE("set_clock_for_testing(nullptr, nullptr) restores the real clock", "[timing]")
{
    std::uint64_t fake_time = 42;
    set_clock_for_testing([&] { return fake_time; }, nullptr);
    REQUIRE(now_ms() == 42);

    set_clock_for_testing(nullptr, nullptr);
    REQUIRE(now_ms() != 42);
}
