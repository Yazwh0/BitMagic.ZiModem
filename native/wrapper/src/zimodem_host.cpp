#define ZIMODEM_HOST_BUILDING_DLL 1
#include "zimodem_host.h"

#include "zimodem_hal/fs_root.h"
#include "zimodem_hal/log.h"
#include "zimodem_hal/net.h"
#include "zimodem_hal/pins.h"
#include "zimodem_hal/serial_port.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

// Defined in zimodem.ino, compiled into zimodem_core.
extern void setup();
extern void loop();

struct zimodem_instance
{
    std::atomic<bool> started{false};
    std::atomic<bool> running{false};
    std::thread worker;

    std::mutex callback_mutex;
    zimodem_serial_out_cb on_serial_out = nullptr;
    zimodem_signal_cb on_signal = nullptr;
    zimodem_log_cb on_log = nullptr;
    zimodem_line_config_cb on_line_config = nullptr;
    void* user_context = nullptr;

    void log(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (on_log)
            on_log(user_context, message.c_str());
    }
};

namespace
{
    // ESP32 SerialConfig bit layout, mirrored from the UART_* macros in the ZIMODEM_HOST
    // branch of zimodem.ino (patches/zimodem/0001-add-zimodem-host-platform-branch.patch)
    // -- and matching the Arduino core's SERIAL_8N1 == 0x800001c that zimodem.ino's
    // DEFAULT_SERIAL_CONFIG resolves to. Duplicated here for the same reason ZIMODEM_PIN_*
    // is duplicated in zimodem_host.h: those macros are internal to the sketch's own
    // translation unit and cannot cross this boundary. Keep in sync.
    constexpr uint32_t kDataBitsMask = 0x0Cu; // bits 2..3: 00=5 01=6 10=7 11=8
    constexpr uint32_t kParityMask   = 0x03u; // bits 0..1: 00=none 10=even 11=odd
    constexpr uint32_t kParityEven   = 0x02u;
    constexpr uint32_t kParityOdd    = 0x03u;
    constexpr uint32_t kStopMask     = 0x30u; // bits 4..5: 01=1 10=1.5 11=2
    constexpr uint32_t kStop15       = 0x20u;
    constexpr uint32_t kStop2        = 0x30u;

    // Decode the raw SerialConfig bitmap into the ABI's separate int outparams. Any
    // pointer may be NULL.
    void decode_serial_config(uint32_t config, int* data_bits, int* parity, int* stop_bits_x10)
    {
        if (data_bits != nullptr)
            *data_bits = 5 + static_cast<int>((config & kDataBitsMask) >> 2);

        if (parity != nullptr)
        {
            switch (config & kParityMask)
            {
            case kParityOdd:  *parity = ZIMODEM_PARITY_ODD;  break;
            case kParityEven: *parity = ZIMODEM_PARITY_EVEN; break;
            default:          *parity = ZIMODEM_PARITY_NONE; break;
            }
        }

        if (stop_bits_x10 != nullptr)
        {
            switch (config & kStopMask)
            {
            case kStop2:  *stop_bits_x10 = 20; break;
            case kStop15: *stop_bits_x10 = 15; break;
            default:      *stop_bits_x10 = 10; break;
            }
        }
    }

    // Enforces the one-instance-per-process rule documented in zimodem_host.h -- the HAL
    // underneath is process-global state, so a second concurrent instance would silently
    // corrupt the first one's serial/pin/fs state rather than behave independently.
    std::mutex g_singleton_mutex;
    zimodem_instance* g_active_instance = nullptr;

    // Real firmware calls loop() in a tight busy-loop because it has nothing else to do.
    // A host CPU does: this keeps latency low (a virtual UART/socket serviced roughly
    // every millisecond is plenty for a modem) without pegging a whole core.
    constexpr auto kLoopInterval = std::chrono::milliseconds(1);

    void run(zimodem_instance* inst)
    {
        try
        {
            setup();
        }
        catch (const std::exception& e)
        {
            inst->log(std::string("setup() threw: ") + e.what());
            inst->running = false;
            return;
        }
        catch (...)
        {
            inst->log("setup() threw an unrecognized exception");
            inst->running = false;
            return;
        }

        while (inst->running.load(std::memory_order_relaxed))
        {
            try
            {
                loop();
            }
            catch (const std::exception& e)
            {
                inst->log(std::string("loop() threw: ") + e.what());
                break;
            }
            catch (...)
            {
                inst->log("loop() threw an unrecognized exception");
                break;
            }
            std::this_thread::sleep_for(kLoopInterval);
        }
        inst->running = false;
    }
}

zimodem_handle zimodem_host_create(const zimodem_host_config* cfg)
{
    // A library has no business silently choosing a filesystem location on the
    // caller's behalf -- see zimodem_host.h's comment on zimodem_host_config::data_dir.
    if (cfg == nullptr || cfg->data_dir == nullptr || cfg->data_dir[0] == '\0')
        return nullptr;

    std::lock_guard<std::mutex> lock(g_singleton_mutex);
    if (g_active_instance != nullptr)
        return nullptr;

    zimodem_hal::fs::set_root(cfg->data_dir);

    zimodem_hal::net::global_init();

    auto* inst = new (std::nothrow) zimodem_instance();
    if (inst == nullptr)
        return nullptr;

    zimodem_hal::serial::set_data_ready_callback([inst]() {
        std::lock_guard<std::mutex> lock(inst->callback_mutex);
        if (inst->on_serial_out)
            inst->on_serial_out(inst->user_context);
    });
    zimodem_hal::pins::set_signal_callback([inst](int pin, int value) {
        std::lock_guard<std::mutex> lock(inst->callback_mutex);
        if (inst->on_signal)
            inst->on_signal(inst->user_context, pin, value);
    });
    zimodem_hal::serial::set_line_config_callback([inst](unsigned long baud, uint32_t config) {
        zimodem_line_config_cb cb;
        void* ctx;
        {
            std::lock_guard<std::mutex> lock(inst->callback_mutex);
            cb = inst->on_line_config;
            ctx = inst->user_context;
        }
        if (cb == nullptr)
            return;
        int data_bits = 0, parity = 0, stop_bits_x10 = 0;
        decode_serial_config(config, &data_bits, &parity, &stop_bits_x10);
        cb(ctx, static_cast<int>(baud), data_bits, parity, stop_bits_x10);
    });
    zimodem_hal::log::set_sink([inst](const std::string& message) { inst->log(message); });

    g_active_instance = inst;
    return reinterpret_cast<zimodem_handle>(inst);
}

void zimodem_host_set_callbacks(zimodem_handle h,
                                 zimodem_serial_out_cb on_serial_out,
                                 zimodem_signal_cb on_signal,
                                 zimodem_log_cb on_log,
                                 zimodem_line_config_cb on_line_config,
                                 void* user_context)
{
    auto* inst = reinterpret_cast<zimodem_instance*>(h);
    if (inst == nullptr)
        return;
    std::lock_guard<std::mutex> lock(inst->callback_mutex);
    inst->on_serial_out = on_serial_out;
    inst->on_signal = on_signal;
    inst->on_log = on_log;
    inst->on_line_config = on_line_config;
    inst->user_context = user_context;
}

int zimodem_host_start(zimodem_handle h)
{
    auto* inst = reinterpret_cast<zimodem_instance*>(h);
    if (inst == nullptr)
        return -1;
    if (inst->started.exchange(true))
        return -1; // already started
    inst->running = true;
    inst->worker = std::thread(run, inst);
    return 0;
}

int zimodem_host_write_serial(zimodem_handle h, const uint8_t* data, size_t len)
{
    auto* inst = reinterpret_cast<zimodem_instance*>(h);
    if (inst == nullptr)
        return -1;
    zimodem_hal::serial::feed_input(data, len);
    return 0;
}

int zimodem_host_rx_available(zimodem_handle h)
{
    auto* inst = reinterpret_cast<zimodem_instance*>(h);
    if (inst == nullptr)
        return 0;
    return zimodem_hal::serial::rx_available() ? 1 : 0;
}

int zimodem_host_rx_read(zimodem_handle h)
{
    auto* inst = reinterpret_cast<zimodem_instance*>(h);
    if (inst == nullptr)
        return -1;
    return zimodem_hal::serial::rx_read();
}

void zimodem_host_set_pin(zimodem_handle h, int pin, int value)
{
    auto* inst = reinterpret_cast<zimodem_instance*>(h);
    if (inst == nullptr)
        return;
    zimodem_hal::pins::digital_write(pin, value);
}

void zimodem_host_get_line_config(zimodem_handle h, int* out_baud, int* out_data_bits,
                                  int* out_parity, int* out_stop_bits_x10)
{
    // Always define every out param (a C# `out` caller has no other initialiser).
    if (out_baud != nullptr)          *out_baud = 0;
    if (out_data_bits != nullptr)     *out_data_bits = 0;
    if (out_parity != nullptr)        *out_parity = ZIMODEM_PARITY_NONE;
    if (out_stop_bits_x10 != nullptr) *out_stop_bits_x10 = 0;

    auto* inst = reinterpret_cast<zimodem_instance*>(h);
    if (inst == nullptr)
        return;

    unsigned long baud = 0;
    uint32_t config = 0;
    zimodem_hal::serial::get_line_config(&baud, &config);

    if (out_baud != nullptr)
        *out_baud = static_cast<int>(baud);
    decode_serial_config(config, out_data_bits, out_parity, out_stop_bits_x10);
}

void zimodem_host_destroy(zimodem_handle h)
{
    auto* inst = reinterpret_cast<zimodem_instance*>(h);
    if (inst == nullptr)
        return;

    inst->running = false;
    if (inst->worker.joinable())
        inst->worker.join();

    zimodem_hal::serial::set_data_ready_callback(nullptr);
    zimodem_hal::serial::set_line_config_callback(nullptr);
    zimodem_hal::pins::set_signal_callback(nullptr);
    zimodem_hal::log::set_sink(nullptr);

    {
        std::lock_guard<std::mutex> lock(g_singleton_mutex);
        if (g_active_instance == inst)
            g_active_instance = nullptr;
    }

    zimodem_hal::net::global_shutdown();
    delete inst;
}
