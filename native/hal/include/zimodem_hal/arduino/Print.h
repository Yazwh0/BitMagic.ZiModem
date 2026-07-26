#pragma once

// Matches the real Arduino core's Print base class (the base of Stream) closely enough
// for zimodem's polymorphic use of it -- e.g. zprint.ino calls `outStream->write(...)`
// and `outStream->flush()` through a `Stream *outStream` pointer (zprint.h), so the
// *virtual* dispatch shape here has to match what a concrete class (ZSerial,
// WiFiClientNode, StringStream) actually overrides, not just provide equivalent-looking
// methods.
//
// print()/println()/printf() ARE needed here (not just prints()/printf() on ZSerial
// itself, as an earlier pass through the sketch assumed): zircmode.ino calls
// `serial.println(...)`/`serial.print(...)` and `current->printf(...)` (current being a
// WiFiClientNode*) directly, relying on these being inherited from Print the same way
// they are on real ESP32 Arduino cores. Centralizing them here means every Stream
// subclass (File, HardwareSerialCompat, WiFiClient, and vendor code's own ZSerial/
// WiFiClientNode) gets them for free, instead of each reimplementing its own copy.

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "WString.h"

class Print
{
public:
    virtual ~Print() = default;

    virtual size_t write(uint8_t b) = 0;

    // Default implementation loops calling write(uint8_t), matching real Arduino Print
    // semantics -- a concrete class only needs to override this if it can do better than
    // one byte at a time.
    virtual size_t write(const uint8_t* buffer, size_t size)
    {
        size_t n = 0;
        while (size--)
        {
            if (write(*buffer++))
                n++;
            else
                break;
        }
        return n;
    }

    size_t write(const char* str)
    {
        if (str == nullptr)
            return 0;
        return write(reinterpret_cast<const uint8_t*>(str), strlen(str));
    }

    size_t write(const char* buffer, size_t size)
    {
        return write(reinterpret_cast<const uint8_t*>(buffer), size);
    }

    virtual int availableForWrite() { return 0; }

    // Empty by default for backward compatibility, matching real Print::flush().
    virtual void flush() {}

    size_t print(const char* s) { return write(s); }
    size_t print(const String& s) { return write(s.c_str()); }

    size_t println() { return write("\r\n"); }
    size_t println(const char* s)
    {
        size_t n = print(s);
        return n + println();
    }
    size_t println(const String& s)
    {
        size_t n = print(s);
        return n + println();
    }

    size_t printf(const char* format, ...)
    {
        char buf[256];
        va_list args;
        va_start(args, format);
        int n = std::vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        if (n <= 0)
            return 0;
        size_t len = static_cast<size_t>(n) < sizeof(buf) ? static_cast<size_t>(n) : sizeof(buf) - 1;
        return write(reinterpret_cast<const uint8_t*>(buf), len);
    }
};
