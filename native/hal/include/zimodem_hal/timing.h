#pragma once

#include <cstdint>
#include <functional>

// Internal HAL API (not Arduino-shaped). arduino/Arduino.h wraps this as the
// global millis()/delay()/yield() the vendored sketch actually calls.
namespace zimodem_hal::timing
{
    using NowFn = std::function<std::uint64_t()>;   // returns milliseconds since some epoch
    using SleepFn = std::function<void(std::uint32_t)>; // blocks (or fast-forwards) for ms

    // Milliseconds since the clock started (real wall clock, unless overridden below).
    std::uint64_t now_ms();

    // Blocks the calling thread for the given duration. On the host build this runs on
    // zimodem's dedicated background thread (see docs/native-wrapper-spec.md section 7.2),
    // so it never blocks a P/Invoke caller.
    void sleep_ms(std::uint32_t ms);

    // Replaces both now_ms() and sleep_ms() for deterministic tests: pass a fake `now` so
    // tests control elapsed time, and a fake `sleep` (typically one that just advances the
    // same fake clock) so delay()-heavy code under test runs instantly. Pass nullptr for
    // both to restore the real wall clock and a real sleep.
    void set_clock_for_testing(NowFn now, SleepFn sleep);
}
