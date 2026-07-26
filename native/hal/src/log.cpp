#include "zimodem_hal/log.h"

#include <cstdio>
#include <mutex>
#include <vector>

namespace zimodem_hal::log
{
    namespace
    {
        std::mutex g_mutex;
        Sink g_sink;
    }

    void set_sink(Sink sink)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_sink = std::move(sink);
    }

    void write(const std::string& message)
    {
        Sink sink_copy;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            sink_copy = g_sink;
        }
        if (sink_copy)
            sink_copy(message);
    }

    void vwritef(const char* format, va_list args)
    {
        va_list args_copy;
        va_copy(args_copy, args);
        int needed = std::vsnprintf(nullptr, 0, format, args_copy);
        va_end(args_copy);
        if (needed < 0)
            return;

        std::vector<char> buf(static_cast<size_t>(needed) + 1);
        std::vsnprintf(buf.data(), buf.size(), format, args);
        write(std::string(buf.data(), static_cast<size_t>(needed)));
    }

    void writef(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vwritef(format, args);
        va_end(args);
    }
}

void zimodem_hal_debug_printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    zimodem_hal::log::vwritef(format, args);
    va_end(args);
}
