#pragma once

#include <cstdint>
#include <functional>

// zimodem treats digitalWrite/digitalRead on a handful of GPIO pins as the modem's
// control signals (DCD/CTS/RTS/RI/DSR/DTR/OTH -- see external/zimodem/TODO for the
// pin table). There is no physical GPIO on a host PC, so this module is an in-memory
// pin-state table: pinMode/digitalWrite/digitalRead behave like real GPIO from the
// vendored code's point of view, and every digitalWrite additionally raises a signal
// callback so a host application can observe modem control-line changes (DCD asserted,
// ring indicator, etc.) as events instead of physical pin toggles.
namespace zimodem_hal::pins
{
    // Mirrors the DEFAULT_PIN_*/DEFAULT_CTS_* constants for the ZIMODEM_HOST platform
    // branch (patches/zimodem/0001-add-zimodem-host-platform-branch.patch). Duplicated
    // here rather than shared with a header: those macros are defined inline inside
    // zimodem.ino itself (compiled into zimodem_core, a separate translation unit this
    // HAL doesn't include from). Keep these in sync if that patch's pin assignments ever
    // change -- named so callers reading/writing a pin can say kCts instead of a bare 9.
    constexpr int kPinDsr = 5;
    constexpr int kPinDtr = 6;
    constexpr int kPinRi = 7;
    constexpr int kPinRts = 8;
    constexpr int kPinCts = 9;
    constexpr int kPinDcd = 10;
    // kPinOth is -1 (no pin) for the host branch -- not a real GPIO, not modeled here.

    // DEFAULT_CTS_ACTIVE/INACTIVE (zimodem.ino) -- active-low, like the rest of this
    // firmware's control lines.
    constexpr int kCtsActive = 0;   // LOW
    constexpr int kCtsInactive = 1; // HIGH

    using SignalCallback = std::function<void(int pin, int value)>;

    void pin_mode(int pin, int mode);
    void digital_write(int pin, int value);
    int digital_read(int pin);

    // Invoked synchronously, on the calling thread, every time digital_write changes a
    // pin's value (not on redundant writes of the same value already held).
    void set_signal_callback(SignalCallback callback);

    // Resets all pin state and clears the callback. Test-only.
    void reset_for_testing();
}
