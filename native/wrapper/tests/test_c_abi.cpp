// Tests the public C ABI (zimodem_host.h) exactly as a real consumer (e.g. the future
// C# binding) would: only the exported zimodem_host_* functions, plain C callback
// function pointers with a user_context, no reaching into zimodem_hal directly to talk
// to the instance under test. zimodem_hal::net is used here only for this test's OWN
// loopback TCP listener (self-contained OS socket wrapper, no shared global state with
// the HAL state living inside zimodem_host's instance), never to touch the instance's
// serial/pin/log state directly.
//
// NOTE: same external-network caveat as test_smoke.cpp -- rt_clock.ino fires a real NTP
// request at startup. Firmware startup here (baud rate + serial config changes, each
// with their own delay(500), then reset/banner, then NTP) takes a few real seconds end
// to end, which is why drain_startup()'s wait is generous.

#include "zimodem_host.h"

#include "zimodem_hal/net.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace
{
    struct Harness
    {
        std::mutex mutex;
        std::string serialOut;
        int lastSignalPin = -1;
        int lastSignalValue = -1;

        static void onSerialOut(void* ctx, const uint8_t* data, size_t len)
        {
            auto* self = static_cast<Harness*>(ctx);
            std::lock_guard<std::mutex> lock(self->mutex);
            self->serialOut.append(reinterpret_cast<const char*>(data), len);
        }
        static void onSignal(void* ctx, int pin, int active)
        {
            auto* self = static_cast<Harness*>(ctx);
            std::lock_guard<std::mutex> lock(self->mutex);
            self->lastSignalPin = pin;
            self->lastSignalValue = active;
        }
        static void onLog(void*, const char*) {}

        std::string snapshotOutput()
        {
            std::lock_guard<std::mutex> lock(mutex);
            return serialOut;
        }
        void clearOutput()
        {
            std::lock_guard<std::mutex> lock(mutex);
            serialOut.clear();
        }
    };

    // Guarantees zimodem_host_destroy() runs even if a REQUIRE fails partway through a
    // test (Catch2 unwinds the test case via exception on failure) -- without this, a
    // failed assertion mid-test leaks the process-wide singleton instance and every
    // later test's zimodem_host_create() spuriously fails too.
    struct HandleGuard
    {
        zimodem_handle h;
        explicit HandleGuard(zimodem_handle handle) : h(handle) {}
        ~HandleGuard()
        {
            if (h)
                zimodem_host_destroy(h);
        }
        HandleGuard(const HandleGuard&) = delete;
    };

    template <typename Predicate>
    bool wait_until(Predicate pred, int timeoutMs = 5000)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (pred())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    // setup() runs a sequence of steps that each take real time on their own -- a baud
    // rate change and a serial config change (each with their own internal delay(500)),
    // then a reset that prints the startup banner, then an NTP send. A "wait until
    // output stops growing" quiescence check is the wrong tool here: the gaps *between*
    // those steps, before anything has been printed yet, already look quiet and cause a
    // premature exit. A fixed, generous sleep covering the whole known startup sequence
    // is simpler and more robust than trying to be clever about it.
    void drain_startup(Harness& harness, int waitMs = 3000)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
        harness.clearOutput();
    }

    void feed(zimodem_handle h, const std::string& s)
    {
        zimodem_host_write_serial(h, reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
}

TEST_CASE("only one zimodem_handle can exist per process", "[c_abi]")
{
    zimodem_host_config cfg{nullptr};
    HandleGuard first(zimodem_host_create(&cfg));
    REQUIRE(first.h != nullptr);

    zimodem_handle second = zimodem_host_create(&cfg);
    REQUIRE(second == nullptr);
}

TEST_CASE("create -> set_callbacks -> start -> AT command round trip -> destroy", "[c_abi]")
{
    Harness harness;
    zimodem_host_config cfg{nullptr};
    HandleGuard guard(zimodem_host_create(&cfg));
    REQUIRE(guard.h != nullptr);

    zimodem_host_set_callbacks(guard.h, Harness::onSerialOut, Harness::onSignal, Harness::onLog, &harness);
    REQUIRE(zimodem_host_start(guard.h) == 0);
    REQUIRE(zimodem_host_start(guard.h) != 0); // starting twice is rejected

    drain_startup(harness); // let the full startup banner flush out and discard it

    feed(guard.h, "AT\r");
    REQUIRE(wait_until([&] { return harness.snapshotOutput().find("OK") != std::string::npos; }));
    REQUIRE(harness.snapshotOutput() == "AT\r\nOK\r\n");
}

TEST_CASE("ATDT dial through the C ABI asserts DCD via the signal callback", "[c_abi]")
{
    Harness harness;
    zimodem_host_config cfg{nullptr};
    HandleGuard guard(zimodem_host_create(&cfg));
    REQUIRE(guard.h != nullptr);
    zimodem_host_set_callbacks(guard.h, Harness::onSerialOut, Harness::onSignal, Harness::onLog, &harness);
    REQUIRE(zimodem_host_start(guard.h) == 0);

    zimodem_hal::net::global_init();
    zimodem_hal::net::TcpListener listener;
    REQUIRE(listener.listen(19301));

    drain_startup(harness);

    feed(guard.h, "ATDT127.0.0.1:19301\r");
    REQUIRE(wait_until([&] { return listener.has_pending_client(); }));
    REQUIRE(wait_until([&] { return harness.snapshotOutput().find("CONNECT") != std::string::npos; }));

    // DEFAULT_PIN_DCD in patches/zimodem/0001 is 10, active-low (0 == asserted).
    REQUIRE(wait_until([&] {
        std::lock_guard<std::mutex> lock(harness.mutex);
        return harness.lastSignalPin == 10 && harness.lastSignalValue == 0;
    }));

    zimodem_hal::net::TcpSocket serverSide = listener.accept();
    REQUIRE(serverSide.is_open());

    zimodem_hal::net::global_shutdown();
}
