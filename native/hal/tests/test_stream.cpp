#include "zimodem_hal/arduino/Stream.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace
{
    // A minimal concrete Stream, standing in for what StringStream (stringstream.h) or
    // ZSerial (serout.h) look like from the base class's point of view: only write(uint8_t)
    // is overridden, so Print's default write(buffer,size) loop is what actually runs.
    class RecordingStream : public Stream
    {
    public:
        std::vector<uint8_t> written;
        std::string toRead;
        size_t readPos = 0;

        size_t write(uint8_t b) override
        {
            written.push_back(b);
            return 1;
        }
        int available() override { return static_cast<int>(toRead.size() - readPos); }
        int read() override { return readPos < toRead.size() ? static_cast<unsigned char>(toRead[readPos++]) : -1; }
        int peek() override { return readPos < toRead.size() ? static_cast<unsigned char>(toRead[readPos]) : -1; }
    };
}

TEST_CASE("Print's default write(buffer,size) loops through write(uint8_t)", "[stream]")
{
    // Called through a Stream* (matching zprint.ino's `outStream->write(...)` pattern),
    // not on the concrete RecordingStream type directly: a derived class that declares
    // only write(uint8_t) hides Print's other write overloads for callers who have the
    // concrete type in hand, exactly as it would for zimodem's own Stream subclasses --
    // which is precisely why ZSerial/WiFiClientNode each redeclare a two-arg write of
    // their own rather than relying on this fallback. Going through the base pointer is
    // what actually exercises Print's default loop implementation.
    RecordingStream s;
    Stream* stream = &s;
    const uint8_t iacDont[3] = {0xFF, 0xFE, 0x01};
    stream->write(iacDont, 3);
    REQUIRE(s.written == std::vector<uint8_t>({0xFF, 0xFE, 0x01}));
}

TEST_CASE("write(const char*) forwards through write(buffer,size)", "[stream]")
{
    RecordingStream s;
    Stream* stream = &s;
    stream->write("hi");
    REQUIRE(s.written == std::vector<uint8_t>({'h', 'i'}));
}

TEST_CASE("polymorphic Stream* dispatch matches pet2asc.ino's stream->available()/read() pattern", "[stream]")
{
    RecordingStream concrete;
    concrete.toRead = "AB";
    Stream* stream = &concrete;

    REQUIRE(stream->available() == 2);
    REQUIRE(stream->read() == 'A');
    REQUIRE(stream->peek() == 'B');
    REQUIRE(stream->read() == 'B');
    REQUIRE(stream->available() == 0);
}

TEST_CASE("flush() defaults to a no-op for backward compatibility", "[stream]")
{
    RecordingStream s;
    Stream* stream = &s;
    stream->flush(); // must not crash; no observable state to assert on
}
