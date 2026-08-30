#include "zimodem_hal/serial_port.h"

#include <deque>
#include <mutex>

namespace zimodem_hal::serial
{
    namespace
    {
        std::mutex g_mutex;
        std::deque<uint8_t> g_input;
        std::deque<uint8_t> g_rx_queue;
        DataReadyCallback g_data_ready_callback;

        // Comfortably above SER_BUFSIZE (128, the firmware's own constant) so its
        // enqueByte/serialOutDeque gate check always passes -- see header for why this
        // HAL deliberately doesn't model real flow control.
        constexpr int kPlentyOfRoom = 4096;
    }

    void set_data_ready_callback(DataReadyCallback cb)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_data_ready_callback = std::move(cb);
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
        DataReadyCallback cb;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (size_t i = 0; i < len; i++)
                g_rx_queue.push_back(data[i]);
            cb = g_data_ready_callback;
        }
        if (len > 0 && cb)
            cb();
        return len;
    }

    bool rx_available()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return !g_rx_queue.empty();
    }

    int rx_read()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_rx_queue.empty())
            return -1;
        uint8_t b = g_rx_queue.front();
        g_rx_queue.pop_front();
        return b;
    }

    int available_for_write()
    {
        return kPlentyOfRoom;
    }

    void reset_for_testing()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_input.clear();
        g_rx_queue.clear();
        g_data_ready_callback = nullptr;
    }
}
