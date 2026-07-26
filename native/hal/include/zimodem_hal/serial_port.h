#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// Internal HAL API (not Arduino-shaped). Backs the `zimodem_hal_serial` global
// (arduino/HardwareSerial.h), which pet2asc.h's ZIMODEM_HOST branch aliases to HWSerial
// -- the modem's actual UART in the vendored sketch. Per docs/native-wrapper-spec.md
// section 7.3, this phase is virtual/in-process only: two byte queues rather than a real
// COM port. feed_input() is how the host app (eventually via the C ABI in
// native/wrapper) pushes bytes in from the "retro computer" side; the output callback is
// how bytes the modem writes get delivered back out.
namespace zimodem_hal::serial
{
    using OutputCallback = std::function<void(const uint8_t* data, size_t len)>;

    // Invoked synchronously on whatever thread calls write() -- on the real wrapper
    // that's zimodem's dedicated background thread (section 7.2).
    void set_output_callback(OutputCallback cb);

    // Host -> modem. Thread-safe: safe to call from a different thread than
    // available()/read()/peek() run on.
    void feed_input(const uint8_t* data, size_t len);

    int available();
    int read();  // -1 if nothing available
    int peek();  // -1 if nothing available, does not consume

    // Modem -> host. Returns len (this queue has no backpressure/capacity limit).
    size_t write(const uint8_t* data, size_t len);

    void reset_for_testing();
}
