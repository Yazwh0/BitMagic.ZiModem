#pragma once

// The umbrella header a real Arduino core provides implicitly for every .ino sketch.
// Since we compile the vendored sketch manually (there is no Arduino IDE build step),
// native/wrapper's translation unit includes this before `#include`-ing zimodem.ino, and
// this header pulls in everything the sketch expects to already be visible: String,
// Stream/Print, IPAddress, WiFi*, FS/SPIFFS, ESP, HWSerial, millis/delay/yield,
// digitalWrite/pinMode/digitalRead, and PROGMEM (a no-op here -- there's no separate
// flash/RAM address space to place data in on a host).
//
// One thing this header depends on being defined *before* it's included: the .ino files
// reach WiFi.h/WiFiClient.h/etc. only because upstream's own INCLUDE_SSH-gated include
// of wifisshclient.h happens to pull in <WiFi.h> transitively (see
// docs/native-wrapper-spec.md section 3) -- our ZIMODEM_HOST branch disables SSH, cutting
// that path, so this header brings in the same surface directly instead.

#include <cstdint>
#include <vector>

#include "ESP.h"
#include "FS.h"
#include "HardwareSerial.h"
#include "IPAddress.h"
#include "Print.h"
#include "Stream.h"
#include "WString.h"
#include "WiFi.h"
#include "WiFiClient.h"
#include "WiFiServer.h"
#include "WiFiUdp.h"

#include "zimodem_hal/log.h"
#include "zimodem_hal/pins.h"
#include "zimodem_hal/timing.h"

#define PROGMEM

#define INPUT 0
#define OUTPUT 1
#define LOW 0
#define HIGH 1

// Matches the ESP32 Arduino core's actual SERIAL_8N1 bit pattern (config bits are
// meaningless on a host UART, but the value has to exist and be usable as a uint32_t).
#define SERIAL_8N1 0x800001cUL

typedef uint8_t byte;

// Real PROGMEM data lives in regular RAM on a host (no separate flash address space to
// place it in -- see the PROGMEM #define above), so reading "from PROGMEM" is just a
// plain dereference.
#define pgm_read_byte_near(addr) (*(const uint8_t*)(addr))

// avr-libc/ESP-core utility zimodem uses for float-to-string formatting (serout.ino's
// printd()). Same contract as the real one: caller guarantees sout is big enough for the
// formatted result, matching the original's own lack of bounds checking.
#include <cstdio>
inline char* dtostrf(double val, signed char width, unsigned char prec, char* sout)
{
    char fmt[32];
    std::snprintf(fmt, sizeof(fmt), "%%%d.%df", static_cast<int>(width), static_cast<int>(prec));
    std::sprintf(sout, fmt, val);
    return sout;
}

inline unsigned long millis() { return static_cast<unsigned long>(zimodem_hal::timing::now_ms()); }
inline void delay(unsigned long ms) { zimodem_hal::timing::sleep_ms(static_cast<uint32_t>(ms)); }
inline void yield() { /* single background thread on the host build; nothing to yield to */ }

inline void pinMode(uint8_t pin, uint8_t mode) { zimodem_hal::pins::pin_mode(pin, mode); }
inline void digitalWrite(uint8_t pin, uint8_t value) { zimodem_hal::pins::digital_write(pin, value); }
inline int digitalRead(uint8_t pin) { return zimodem_hal::pins::digital_read(pin); }

// zircmode.ino's random nickname generation (`randomSeed(millis()); random(1,999)`) is
// the only user; exact PRNG bit-for-bit fidelity with a real ESP32 doesn't matter here
// (nothing depends on reproducing its specific sequence), just plausible randomness.
#include <cstdlib>
inline void randomSeed(unsigned long seed) { std::srand(static_cast<unsigned>(seed)); }
inline long random(long max) { return max > 0 ? std::rand() % max : 0; }
inline long random(long min, long max) { return max > min ? min + (std::rand() % (max - min)) : min; }

// itoa() is an MSVC CRT / avr-libc extension, not standard C/C++ and not present in
// glibc -- compiles fine on Windows (MSVC provides its own), fails on Linux ("'itoa' was
// not declared in this scope"). Only #ifndef _WIN32 guarded so it doesn't collide with
// MSVC's own declaration there. serout.ino's only call site (ZSerial::printi) always
// passes base 10, but this matches the real function's general (value, buffer, base)
// contract rather than hard-coding that.
#ifndef _WIN32
inline char* itoa(int value, char* str, int base)
{
    char* out = str;
    bool negative = base == 10 && value < 0;
    unsigned int uvalue = negative ? static_cast<unsigned int>(-(value + 1)) + 1u : static_cast<unsigned int>(value);

    char digits[32];
    int count = 0;
    do
    {
        int digit = static_cast<int>(uvalue % static_cast<unsigned int>(base));
        digits[count++] = static_cast<char>(digit < 10 ? '0' + digit : 'a' + (digit - 10));
        uvalue /= static_cast<unsigned int>(base);
    } while (uvalue != 0);

    if (negative)
        *out++ = '-';
    while (count > 0)
        *out++ = digits[--count];
    *out = '\0';
    return str;
}
#endif
