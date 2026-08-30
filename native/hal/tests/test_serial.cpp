#include "zimodem_hal/arduino/HardwareSerial.h"
#include "zimodem_hal/serial_port.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

namespace
{
    struct SerialGuard
    {
        SerialGuard() { zimodem_hal::serial::reset_for_testing(); }
        ~SerialGuard() { zimodem_hal::serial::reset_for_testing(); }
    };
}

TEST_CASE("feed_input makes bytes available to read, in order (host -> modem direction)", "[serial]")
{
    SerialGuard guard;
    const uint8_t bytes[] = {'A', 'T', '\r'};
    zimodem_hal::serial::feed_input(bytes, sizeof(bytes));

    REQUIRE(zimodem_hal::serial::available() == 3);
    REQUIRE(zimodem_hal::serial::peek() == 'A');
    REQUIRE(zimodem_hal::serial::read() == 'A');
    REQUIRE(zimodem_hal::serial::read() == 'T');
    REQUIRE(zimodem_hal::serial::read() == '\r');
    REQUIRE(zimodem_hal::serial::available() == 0);
    REQUIRE(zimodem_hal::serial::read() == -1);
}

TEST_CASE("write fires the data-ready callback and queues bytes for rx_read (modem -> host direction)", "[serial]")
{
    SerialGuard guard;
    int ready_calls = 0;
    zimodem_hal::serial::set_data_ready_callback([&]() { ready_calls++; });

    const uint8_t reply[] = {'O', 'K', '\r', '\n'};
    zimodem_hal::serial::write(reply, sizeof(reply));

    REQUIRE(ready_calls == 1);
    std::vector<uint8_t> captured;
    while (zimodem_hal::serial::rx_available())
        captured.push_back(static_cast<uint8_t>(zimodem_hal::serial::rx_read()));
    REQUIRE(captured == std::vector<uint8_t>({'O', 'K', '\r', '\n'}));
    REQUIRE(zimodem_hal::serial::rx_read() == -1);
}

TEST_CASE("the rx queue is unbounded and undelayed -- no capacity, flow control, or timing model", "[serial]")
{
    // Deliberate: see serial_port.h's file comment. A UART chip emulation on the other
    // side of rx_available/rx_read owns baud-rate pacing, its own bounded FIFO, and
    // overrun -- this queue is just the wire, real-time and uncapped.
    SerialGuard guard;
    std::vector<uint8_t> lots(1000, 'A');
    zimodem_hal::serial::write(lots.data(), lots.size());
    for (size_t i = 0; i < lots.size(); i++)
        REQUIRE(zimodem_hal::serial::rx_read() == 'A');
    REQUIRE(zimodem_hal::serial::rx_read() == -1);
}

TEST_CASE("available_for_write always reports plenty of room", "[serial]")
{
    // See serial_port.h's file comment -- this HAL deliberately doesn't model flow
    // control, so this is unconditional, not derived from any state.
    SerialGuard guard;
    REQUIRE(zimodem_hal::serial::available_for_write() > 0);
}

TEST_CASE("HardwareSerialCompat routes available/read/peek/write through the serial_port queues", "[serial]")
{
    SerialGuard guard;
    HardwareSerialCompat serial;

    const uint8_t in[] = {'X', 'Y'};
    zimodem_hal::serial::feed_input(in, sizeof(in));
    REQUIRE(serial.available() == 2);
    REQUIRE(serial.peek() == 'X');
    REQUIRE(serial.read() == 'X');

    serial.write(static_cast<uint8_t>('Z'));
    REQUIRE(zimodem_hal::serial::rx_read() == 'Z');
}

TEST_CASE("readBytes collects exactly `length` bytes when they arrive before the timeout", "[serial]")
{
    SerialGuard guard;
    HardwareSerialCompat serial;
    serial.setTimeout(200);

    const uint8_t in[] = {1, 2, 3, 4};
    zimodem_hal::serial::feed_input(in, sizeof(in));

    uint8_t buf[4] = {0};
    size_t n = serial.readBytes(buf, 4);
    REQUIRE(n == 4);
    REQUIRE(std::vector<uint8_t>(buf, buf + 4) == std::vector<uint8_t>({1, 2, 3, 4}));
}

TEST_CASE("readBytes gives up after the timeout and returns however many bytes it got", "[serial]")
{
    SerialGuard guard;
    HardwareSerialCompat serial;
    serial.setTimeout(30); // short, so the test doesn't take long

    const uint8_t in[] = {1, 2};
    zimodem_hal::serial::feed_input(in, sizeof(in));

    uint8_t buf[5] = {0};
    size_t n = serial.readBytes(buf, 5); // only 2 bytes ever arrive
    REQUIRE(n == 2);
}

TEST_CASE("printf formats and writes through to rx_read", "[serial]")
{
    SerialGuard guard;
    HardwareSerialCompat serial;

    // Matches zcommand.ino's `HWSerial.printf("%d%s", rcvdCrc8, EOLN.c_str());`
    serial.printf("%d%s", 42, "\r\n");

    std::string captured;
    while (zimodem_hal::serial::rx_available())
        captured.push_back(static_cast<char>(zimodem_hal::serial::rx_read()));
    REQUIRE(captured == "42\r\n");
}
