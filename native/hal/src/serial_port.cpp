#include "zimodem_hal/serial_port.h"

#include <deque>
#include <mutex>

namespace zimodem_hal::serial
{
    namespace
    {
        std::mutex g_mutex;
        std::deque<uint8_t> g_input;
        OutputCallback g_output_callback;
    }

    void set_output_callback(OutputCallback cb)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_output_callback = std::move(cb);
    }

    void feed_input(const uint8_t* data, size_t len)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (size_t i = 0; i < len; i++)
            g_input.push_back(data[i]);
    }

    int available()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return static_cast<int>(g_input.size());
    }

    int read()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_input.empty())
            return -1;
        uint8_t b = g_input.front();
        g_input.pop_front();
        return b;
    }

    int peek()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_input.empty())
            return -1;
        return g_input.front();
    }

    size_t write(const uint8_t* data, size_t len)
    {
        OutputCallback cb;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            cb = g_output_callback;
        }
        if (cb)
            cb(data, len);
        return len;
    }

    void reset_for_testing()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_input.clear();
        g_output_callback = nullptr;
    }
}
