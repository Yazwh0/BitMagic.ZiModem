#pragma once

// Public C ABI for the zimodem host wrapper. See docs/native-wrapper-spec.md section 8.
// This is the only header a C# P/Invoke layer (or any other consumer) needs -- no C++
// types cross this boundary, no exceptions cross it either (every exception from the
// vendored/HAL code is caught inside and reported via the log callback).
//
// IMPORTANT, and a real constraint worth understanding before using this: the HAL
// underneath (zimodem_hal::serial/pins/fs/log) is process-global state, matching the
// vendored firmware's own assumption that it's the only thing running on the chip.
// That means only one zimodem_handle may exist per process at a time --
// zimodem_host_create() returns NULL if one already exists. This mirrors real hardware
// (there is only ever one modem) rather than being an arbitrary limitation to lift later.
//
// Sequencing deviates slightly from the spec's first-draft sketch: create() no longer
// auto-starts the background thread. Starting it immediately raced against the caller's
// chance to register callbacks via zimodem_host_set_callbacks() -- any debugPrintf/
// signal/serial-out activity during that window would've been silently dropped. The
// corrected sequence is create() -> set_callbacks() -> start() -> ... -> destroy().

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
# if defined(ZIMODEM_HOST_BUILDING_DLL)
#  define ZIMODEM_API __declspec(dllexport)
# else
#  define ZIMODEM_API __declspec(dllimport)
# endif
#else
# define ZIMODEM_API __attribute__((visibility("default")))
#endif

typedef struct zimodem_instance* zimodem_handle;

// Pin numbers for zimodem_host_set_pin/zimodem_signal_cb -- mirrors the DEFAULT_PIN_*
// constants for the ZIMODEM_HOST platform branch
// (patches/zimodem/0001-add-zimodem-host-platform-branch.patch). Duplicated here
// (rather than shared with a header) since this file has to stay plain C for the
// P/Invoke boundary, and those macros are internal to zimodem.ino's own translation
// unit. Keep in sync if that patch's pin assignments ever change. ZIMODEM_PIN_OTH is
// intentionally absent -- it's -1 (no pin) for this platform branch, not a real GPIO.
#define ZIMODEM_PIN_DSR 5
#define ZIMODEM_PIN_DTR 6
#define ZIMODEM_PIN_RI  7
#define ZIMODEM_PIN_RTS 8
#define ZIMODEM_PIN_CTS 9
#define ZIMODEM_PIN_DCD 10

// DEFAULT_CTS_ACTIVE/INACTIVE (zimodem.ino) -- active-low, like the rest of this
// firmware's control lines. Applies to all the pins above, not just CTS.
#define ZIMODEM_PIN_ACTIVE 0   // LOW
#define ZIMODEM_PIN_INACTIVE 1 // HIGH

typedef struct zimodem_host_config
{
    // Host directory the emulated SPIFFS filesystem (config, phonebook, logs) is rooted
    // under. Required: non-NULL, non-empty. zimodem_host_create() fails (returns NULL)
    // if this is missing -- a library has no business silently picking a filesystem
    // location on the caller's behalf (e.g. an OS temp directory, which would also mean
    // config/phonebook silently fail to persist across restarts). The caller decides
    // where their data lives; if you want a throwaway directory, compute one yourself
    // (e.g. an OS temp path) and pass it explicitly.
    const char* data_dir;
} zimodem_host_config;

// Invoked synchronously on the background thread started by zimodem_host_start() --
// do not block in these, and do not call zimodem_host_destroy() from within one (it
// joins that same thread and will deadlock). Calling zimodem_host_write_serial() from a
// callback is fine (it's just a thread-safe queue push).
//
// on_serial_out carries no data -- it fires whenever the modem has queued at least one
// byte for the host, as a "go drain it" wake-up rather than the payload itself. Call
// zimodem_host_rx_read() in a loop from within this callback (or from any other thread,
// at any time) until zimodem_host_rx_available() says empty. This queue is unbounded
// and untracked for overrun on this side deliberately -- a real UART chip (e.g. a
// TL16C2550) owns its own fixed-depth FIFO and overrun behavior; if you're emulating
// one, that belongs in your own code on top of this, not here.
//
// on_line_config fires whenever the firmware changes the serial line settings (baud,
// data bits, parity, or stop bits) -- an AT config, an ATSxx write, or the power-on
// default being applied -- and NOT for a redundant re-apply of the current settings.
// Its argument layout matches zimodem_host_get_line_config's outparams (parity is a
// ZIMODEM_PARITY_* value; stop_bits_x10 is stop bits times ten).
typedef void (*zimodem_serial_out_cb)(void* user_context);
typedef void (*zimodem_signal_cb)(void* user_context, int pin, int active);
typedef void (*zimodem_log_cb)(void* user_context, const char* message);
typedef void (*zimodem_line_config_cb)(void* user_context,
                                       int baud, int data_bits, int parity, int stop_bits_x10);

// Allocates an instance and configures the data directory. Does not start the background
// thread. Returns NULL if cfg is NULL or cfg->data_dir is NULL/empty, if an instance
// already exists in this process (see the note above), or if allocation fails.
ZIMODEM_API zimodem_handle zimodem_host_create(const zimodem_host_config* cfg);

// Registers callbacks, replacing any previously registered ones. Call this before
// zimodem_host_start() to avoid missing early output. Any of the callback pointers may
// be NULL to stop receiving that kind of event.
ZIMODEM_API void zimodem_host_set_callbacks(zimodem_handle h,
                                             zimodem_serial_out_cb on_serial_out,
                                             zimodem_signal_cb on_signal,
                                             zimodem_log_cb on_log,
                                             zimodem_line_config_cb on_line_config,
                                             void* user_context);

// Starts the background thread: runs the vendored sketch's setup() once, then loop()
// repeatedly until zimodem_host_destroy(). Returns 0 on success, non-zero if h is
// invalid or already started.
ZIMODEM_API int zimodem_host_start(zimodem_handle h);

// Host -> modem. Thread-safe and non-blocking: enqueues bytes for the background
// thread's next loop() iteration to consume. Returns 0 on success, non-zero if h is
// invalid.
ZIMODEM_API int zimodem_host_write_serial(zimodem_handle h, const uint8_t* data, size_t len);

// Modem -> host, poll side. zimodem_host_rx_available() returns non-zero if a byte is
// waiting; zimodem_host_rx_read() dequeues and returns it (-1 if empty, or if h is
// invalid). Both are thread-safe and callable from any thread, at any time.
ZIMODEM_API int zimodem_host_rx_available(zimodem_handle h);
ZIMODEM_API int zimodem_host_rx_read(zimodem_handle h);

// Host -> modem pin write (the reverse of zimodem_signal_cb, which is modem -> host
// only) -- e.g. drive ZIMODEM_PIN_CTS to ZIMODEM_PIN_INACTIVE to tell the modem to hold
// off sending, matching real ESP32 hardware flow control
// (uart_set_hw_flow_ctrl(..., UART_HW_FLOWCTRL_CTS_RTS, ...) pausing transmission when
// CTS is deasserted): the vendored firmware's own serout.ino already checks this exact
// pin via digitalRead(pinCTS), and the availableForWrite() value its enqueByte/
// serialOutDeque gate on is now computed directly from it. IMPORTANT: don't leave CTS
// deasserted indefinitely with nothing planning to reassert it -- enqueByte blocks the
// firmware's background thread in a busy-wait once its own internal buffer fills, with
// no timeout, until CTS goes active again; leaving it inactive forever hangs the modem.
// Thread-safe: safe to call from any thread, at any time. A no-op if h is invalid.
ZIMODEM_API void zimodem_host_set_pin(zimodem_handle h, int pin, int value);

// Parity values reported by zimodem_host_get_line_config()'s out_parity.
#define ZIMODEM_PARITY_NONE 0
#define ZIMODEM_PARITY_ODD  1
#define ZIMODEM_PARITY_EVEN 2

// The modem's current serial line settings, as last applied by the vendored firmware
// (its power-on default, or a later AT / ATSxx change). A UART emulation on this side of
// the ABI can use these to verify its own divisor and framing match the modem's -- a
// mismatch is exactly what produces garbage on real hardware. Any out pointer may be
// NULL. Every non-NULL out param is always written (0 / ZIMODEM_PARITY_NONE if h is
// invalid). While out_baud is 0 the line has not been configured yet -- treat that as
// "not ready" and ignore the other fields.
//
//   out_baud          - bits per second, e.g. 1200 or 115200
//   out_data_bits     - 5..8
//   out_parity        - one of ZIMODEM_PARITY_* above
//   out_stop_bits_x10 - stop bits times ten: 10 (one), 15 (one and a half), or 20 (two)
//
// Thread-safe: safe to call from any thread, at any time.
ZIMODEM_API void zimodem_host_get_line_config(zimodem_handle h,
                                              int* out_baud,
                                              int* out_data_bits,
                                              int* out_parity,
                                              int* out_stop_bits_x10);

// Stops the background thread (if started) and joins it, then frees the instance. h is
// invalid after this call. Safe to call from any thread other than the background
// thread itself; blocks until that thread exits.
ZIMODEM_API void zimodem_host_destroy(zimodem_handle h);

#ifdef __cplusplus
}
#endif
