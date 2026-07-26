#include "zimodem_hal/pins.h"

#include <mutex>
#include <unordered_map>

namespace zimodem_hal::pins
{
    namespace
    {
        std::mutex g_mutex;
        std::unordered_map<int, int> g_mode;
        std::unordered_map<int, int> g_value;
        SignalCallback g_callback;
    }

    void pin_mode(int pin, int mode)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_mode[pin] = mode;
    }

    void digital_write(int pin, int value)
    {
        SignalCallback callback_copy;
        bool changed;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_value.find(pin);
            changed = (it == g_value.end()) || (it->second != value);
            g_value[pin] = value;
            callback_copy = g_callback;
        }
        if (changed && callback_copy)
            callback_copy(pin, value);
    }

    int digital_read(int pin)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_value.find(pin);
        return it == g_value.end() ? 0 : it->second;
    }

    void set_signal_callback(SignalCallback callback)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_callback = std::move(callback);
    }

    void reset_for_testing()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_mode.clear();
        g_value.clear();
        g_callback = nullptr;
    }
}
