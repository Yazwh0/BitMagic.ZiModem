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
    std::lock_guard<std::mutex> lock(g_singleton_mutex);
    if (g_active_instance != nullptr)
        return nullptr;

    if (cfg != nullptr && cfg->data_dir != nullptr)
        zimodem_hal::fs::set_root(cfg->data_dir);

    zimodem_hal::net::global_init();

    auto* inst = new (std::nothrow) zimodem_instance();
    if (inst == nullptr)
        return nullptr;

    zimodem_hal::serial::set_output_callback([inst](const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(inst->callback_mutex);
        if (inst->on_serial_out)
            inst->on_serial_out(inst->user_context, data, len);
    });
    zimodem_hal::pins::set_signal_callback([inst](int pin, int value) {
        std::lock_guard<std::mutex> lock(inst->callback_mutex);
        if (inst->on_signal)
            inst->on_signal(inst->user_context, pin, value);
    });
    zimodem_hal::log::set_sink([inst](const std::string& message) { inst->log(message); });

    g_active_instance = inst;
    return reinterpret_cast<zimodem_handle>(inst);
}

void zimodem_host_set_callbacks(zimodem_handle h,
                                 zimodem_serial_out_cb on_serial_out,
                                 zimodem_signal_cb on_signal,
                                 zimodem_log_cb on_log,
                                 void* user_context)
{
    auto* inst = reinterpret_cast<zimodem_instance*>(h);
    if (inst == nullptr)
        return;
    std::lock_guard<std::mutex> lock(inst->callback_mutex);
    inst->on_serial_out = on_serial_out;
    inst->on_signal = on_signal;
    inst->on_log = on_log;
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

void zimodem_host_destroy(zimodem_handle h)
{
    auto* inst = reinterpret_cast<zimodem_instance*>(h);
    if (inst == nullptr)
        return;

    inst->running = false;
    if (inst->worker.joinable())
        inst->worker.join();

    zimodem_hal::serial::set_output_callback(nullptr);
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
