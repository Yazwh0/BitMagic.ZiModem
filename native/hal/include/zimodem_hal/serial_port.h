#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// Internal HAL API (not Arduino-shaped). Backs the `zimodem_hal_serial` global
// (arduino/HardwareSerial.h), which pet2asc.h's ZIMODEM_HOST branch aliases to HWSerial
// -- the modem's actual UART in the vendored sketch. Per docs/native-wrapper-spec.md
// section 7.3, this phase is virtual/in-process only: two byte queues rather than a real
// COM port. feed_input() is how the host app (eventually via the C ABI in
// native/wrapper) pushes bytes in from the "retro computer" side; the modem -> host
// direction is poll-based (rx_available/rx_read below) rather than instant synchronous
// delivery, so a real UART emulation sitting on the other side of the C ABI can drain it
// at its own pace instead of being forced to consume every byte the instant it's
// produced.
//
// Deliberately real-time and unbounded in both directions, with no notion of baud rate
// or flow control -- that's a conscious split, not an oversight. zimodem_host runs on
// its own background thread, entirely decoupled from whatever clock domain a consumer
// (e.g. an emulator) lives on; real-time is exactly right for what this HAL actually
// does (NTP requests, TCP sockets, the vendored firmware's own real-millisecond timing
// like the +++ escape's 900ms silence requirement), but wrong for anything a consumer's
// own clock needs to observe consistently (paced delivery that should pause when an
// emulator pauses, for instance). A UART chip emulation (e.g. a TL16C2550) sitting on
// the far side of rx_available/rx_read/write_serial is the right place for baud-rate
// pacing, its own bounded FIFO, and overrun -- and since this queue is unbounded, that
// consumer can safely stop draining it whenever it wants (its own FIFO is full, its own
// clock says not yet) with zero data-loss risk on this side. That also means it doesn't
// need a flow-control signal between the two: backpressure falls out for free from the
// consumer simply choosing when to poll.
namespace zimodem_hal::serial
{
    // Invoked synchronously (on zimodem's background thread) whenever write() adds a
    // byte. Carries no data -- it's a "go call rx_read() until rx_available() says
    // empty" wake-up, not the payload itself, so the consumer decides when to drain.
    using DataReadyCallback = std::function<void()>;
    void set_data_ready_callback(DataReadyCallback cb);

    // Host -> modem. Thread-safe: safe to call from a different thread than
    // available()/read()/peek() run on.
    void feed_input(const uint8_t* data, size_t len);

    int available();
    int read();  // -1 if nothing available
    int peek();  // -1 if nothing available, does not consume

    // Modem -> host. Queues every byte (unbounded -- see the file comment). Returns len,
    // matching Arduino Stream::write()'s contract (the vendored firmware doesn't
    // meaningfully check this return value).
    size_t write(const uint8_t* data, size_t len);

    bool rx_available();  // true if rx_read() has something to return
    int rx_read();         // -1 if empty, otherwise dequeues and returns the byte

    // Always reports plenty of room -- see the file comment for why this HAL doesn't
    // model flow control. Backs HardwareSerialCompat::availableForWrite(), which the
    // vendored firmware's own logic checks (serout.ino's enqueByte/serialOutDeque)
    // against its own SER_BUFSIZE constant (128); this being unconditionally large means
    // that check always passes, so the firmware never throttles or stalls on it.
    int available_for_write();

    // Line settings, as last passed to HardwareSerialCompat::begin() by the vendored
    // firmware (its power-on default, or a later AT / ATSxx change). This HAL still does
    // not act on them -- it stays baud-agnostic by design (see the file comment) -- they
    // are recorded purely so a UART emulation on the far side of the C ABI can check its
    // own divisor / framing against the modem's; a mismatch is what produces garbage on
    // real hardware. `config` is the raw ESP32 SerialConfig bitmap (data bits, parity,
    // stop bits); baud 0 / config 0 means begin() has not run yet. Thread-safe.
    void set_line_config(unsigned long baud, uint32_t config);
    void set_line_baud(unsigned long baud);
    void get_line_config(unsigned long* baud, uint32_t* config);

    // Invoked whenever set_line_config()/set_line_baud() records a value that differs
    // from the last one (so a begin() that repeats the current settings does not fire
    // it). Called OUTSIDE the internal lock, on whatever thread called begin() -- for
    // the vendored firmware that is its background loop() thread. Registering replaces
    // any previous callback; pass nullptr to clear. Keep it non-blocking.
    using LineConfigCallback = std::function<void(unsigned long baud, uint32_t config)>;
    void set_line_config_callback(LineConfigCallback cb);

    void reset_for_testing();
}
