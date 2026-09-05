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

        // Last values seen by HardwareSerialCompat::begin(); 0 = not configured yet.
        unsigned long g_line_baud = 0;
        uint32_t g_line_config = 0;
        LineConfigCallback g_line_config_callback;

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

    void set_line_config(unsigned long baud, uint32_t config)
    {
        LineConfigCallback cb;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_line_baud == baud && g_line_config == config)
                return; // no change -- don't fire
            g_line_baud = baud;
            g_line_config = config;
            cb = g_line_config_callback;
        }
        if (cb)
            cb(baud, config); // outside the lock: the callback may re-enter this HAL
    }

    void set_line_baud(unsigned long baud)
    {
        LineConfigCallback cb;
        uint32_t config = 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_line_baud == baud)
                return;
            g_line_baud = baud;
            config = g_line_config;
            cb = g_line_config_callback;
        }
        if (cb)
            cb(baud, config);
    }

    void get_line_config(unsigned long* baud, uint32_t* config)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (baud != nullptr)
            *baud = g_line_baud;
        if (config != nullptr)
            *config = g_line_config;
    }

    void set_line_config_callback(LineConfigCallback cb)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_line_config_callback = std::move(cb);
    }

    void reset_for_testing()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_input.clear();
        g_rx_queue.clear();
        g_data_ready_callback = nullptr;
        g_line_baud = 0;
        g_line_config = 0;
        g_line_config_callback = nullptr;
    }
}
