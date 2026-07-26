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

TEST_CASE("write invokes the registered output callback (modem -> host direction)", "[serial]")
{
    SerialGuard guard;
    std::vector<uint8_t> captured;
    zimodem_hal::serial::set_output_callback([&](const uint8_t* data, size_t len) {
        captured.assign(data, data + len);
    });

    const uint8_t reply[] = {'O', 'K', '\r', '\n'};
    zimodem_hal::serial::write(reply, sizeof(reply));

    REQUIRE(captured == std::vector<uint8_t>({'O', 'K', '\r', '\n'}));
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

    std::vector<uint8_t> out;
    zimodem_hal::serial::set_output_callback([&](const uint8_t* data, size_t len) {
        out.assign(data, data + len);
    });
    serial.write(static_cast<uint8_t>('Z'));
    REQUIRE(out == std::vector<uint8_t>({'Z'}));
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

TEST_CASE("printf formats and writes through the output callback", "[serial]")
{
    SerialGuard guard;
    HardwareSerialCompat serial;
    std::string captured;
    zimodem_hal::serial::set_output_callback([&](const uint8_t* data, size_t len) {
        captured.assign(reinterpret_cast<const char*>(data), len);
    });

    // Matches zcommand.ino's `HWSerial.printf("%d%s", rcvdCrc8, EOLN.c_str());`
    serial.printf("%d%s", 42, "\r\n");
    REQUIRE(captured == "42\r\n");
}
