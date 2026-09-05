#pragma once

// The modem's virtual UART. pet2asc.h's ZIMODEM_HOST branch (patches/zimodem/0002)
// `#define HWSerial zimodem_hal_serial`, mirroring the ESP8266 branch's own
// `#define HWSerial Serial` idiom. Method surface matches every HWSerial.* call found
// across the compiled sketch (verified by grep): begin, setRxBufferSize, setTimeout,
// available, read, peek, write, availableForWrite, flush, readBytes, printf.

#include <cstdint>

#include "Stream.h"
#include "zimodem_hal/serial_port.h"
#include "zimodem_hal/timing.h"

class HardwareSerialCompat : public Stream
{
public:
    // Baud rate and framing are not acted on -- see serial_port.h's file comment: this
    // HAL is deliberately real-time and doesn't model flow control or transmission
    // pacing. A UART chip emulation on the other side of rx_available/rx_read is where
    // baud-rate-paced timing belongs, tracking its own (emulated) clock. They are still
    // recorded via set_line_config() so that consumer can check its settings match the
    // modem's.
    void begin(unsigned long baud) { zimodem_hal::serial::set_line_baud(baud); }
    void begin(unsigned long baud, uint32_t config) { zimodem_hal::serial::set_line_config(baud, config); }
    void begin(unsigned long baud, uint32_t config, int /*rxPin*/, int /*txPin*/) { zimodem_hal::serial::set_line_config(baud, config); }

    void setRxBufferSize(size_t) {}
    void setTimeout(unsigned long ms) { timeoutMs_ = ms; }

    int available() override { return zimodem_hal::serial::available(); }
    int read() override { return zimodem_hal::serial::read(); }
    int peek() override { return zimodem_hal::serial::peek(); }

    size_t write(uint8_t b) override { return zimodem_hal::serial::write(&b, 1); }
    size_t write(const uint8_t* buf, size_t size) override { return zimodem_hal::serial::write(buf, size); }
    using Print::write;

    // Always plenty of room -- see zimodem_hal::serial::available_for_write and this
    // file's own comment on begin() for why this HAL doesn't model flow control.
    int availableForWrite() override { return zimodem_hal::serial::available_for_write(); }
    void flush() override {}

    // Matches real Arduino Stream::readBytes: polls read() until `length` bytes are
    // collected or setTimeout()'s duration elapses with no new byte.
    size_t readBytes(uint8_t* buffer, size_t length)
    {
        size_t count = 0;
        unsigned long deadline = static_cast<unsigned long>(zimodem_hal::timing::now_ms()) + timeoutMs_;
        while (count < length)
        {
            int c = read();
            if (c >= 0)
            {
                buffer[count++] = static_cast<uint8_t>(c);
                deadline = static_cast<unsigned long>(zimodem_hal::timing::now_ms()) + timeoutMs_;
            }
            else
            {
                if (static_cast<unsigned long>(zimodem_hal::timing::now_ms()) >= deadline)
                    break;
                zimodem_hal::timing::sleep_ms(1);
            }
        }
        return count;
    }
    size_t readBytes(char* buffer, size_t length) { return readBytes(reinterpret_cast<uint8_t*>(buffer), length); }

    // print()/println()/printf() come from Print.

private:
    unsigned long timeoutMs_ = 1000;
};

extern HardwareSerialCompat zimodem_hal_serial;
