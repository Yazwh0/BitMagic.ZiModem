#pragma once

#include <cstdarg>
#include <functional>
#include <string>

// Backs the vendored sketch's debugPrintf(...)/DBSerial output (zimodem.ino's own debug
// channel, distinct from the modem's serial byte stream in serial.h). There is no second
// physical UART on a host, so debug output is routed to a single callback-based sink.
namespace zimodem_hal::log
{
    using Sink = std::function<void(const std::string& message)>;

    // Registers where formatted debug lines go. Pass nullptr to discard them (the default
    // is a no-op sink, not stderr, so tests aren't noisy by default).
    void set_sink(Sink sink);

    void write(const std::string& message);
    void writef(const char* format, ...);
    void vwritef(const char* format, va_list args);
}

// Matches the signature `debugPrintf` is #define'd to for the ZIMODEM_HOST platform
// branch (see patches/zimodem/0001-add-zimodem-host-platform-branch.patch). Declared at
// global scope because the vendored sketch calls it unqualified.
void zimodem_hal_debug_printf(const char* format, ...);
