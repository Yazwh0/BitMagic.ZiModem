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
typedef void (*zimodem_serial_out_cb)(void* user_context, const uint8_t* data, size_t len);
typedef void (*zimodem_signal_cb)(void* user_context, int pin, int active);
typedef void (*zimodem_log_cb)(void* user_context, const char* message);

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
                                             void* user_context);

// Starts the background thread: runs the vendored sketch's setup() once, then loop()
// repeatedly until zimodem_host_destroy(). Returns 0 on success, non-zero if h is
// invalid or already started.
ZIMODEM_API int zimodem_host_start(zimodem_handle h);

// Host -> modem. Thread-safe and non-blocking: enqueues bytes for the background
// thread's next loop() iteration to consume. Returns 0 on success, non-zero if h is
// invalid.
ZIMODEM_API int zimodem_host_write_serial(zimodem_handle h, const uint8_t* data, size_t len);

// Stops the background thread (if started) and joins it, then frees the instance. h is
// invalid after this call. Safe to call from any thread other than the background
// thread itself; blocks until that thread exits.
ZIMODEM_API void zimodem_host_destroy(zimodem_handle h);

#ifdef __cplusplus
}
#endif
