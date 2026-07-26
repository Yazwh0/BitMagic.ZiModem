#include "zimodem_hal/timing.h"

#include <chrono>
#include <mutex>
#include <thread>

namespace zimodem_hal::timing
{
    namespace
    {
        std::mutex g_mutex;
        NowFn g_now_override;
        SleepFn g_sleep_override;

        std::uint64_t real_now_ms()
        {
            using namespace std::chrono;
            return static_cast<std::uint64_t>(
                duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
        }

        void real_sleep_ms(std::uint32_t ms)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
    }

    std::uint64_t now_ms()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_now_override ? g_now_override() : real_now_ms();
    }

    void sleep_ms(std::uint32_t ms)
    {
        SleepFn sleep_fn;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            sleep_fn = g_sleep_override;
        }
        if (sleep_fn)
            sleep_fn(ms);
        else
            real_sleep_ms(ms);
    }

    void set_clock_for_testing(NowFn now, SleepFn sleep)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_now_override = std::move(now);
        g_sleep_override = std::move(sleep);
    }
}
