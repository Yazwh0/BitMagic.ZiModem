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
