#pragma once

// Matches the subset of Arduino's IPAddress used by zimodem (verified by grep across
// external/zimodem/zimodem): default/uint32_t/four-octet constructors, octet indexing
// via operator[] (connSettings.ino's IPtoStr does `(*ip)[0]` etc.), and toString().
// IPv4 only -- zimodem never touches IPv6.

#include <cstdint>
#include <cstdio>

#include "WString.h"

class IPAddress
{
public:
    IPAddress() = default;

    explicit IPAddress(uint32_t address) { bytes_[0] = static_cast<uint8_t>(address); bytes_[1] = static_cast<uint8_t>(address >> 8); bytes_[2] = static_cast<uint8_t>(address >> 16); bytes_[3] = static_cast<uint8_t>(address >> 24); }

    IPAddress(uint8_t o1, uint8_t o2, uint8_t o3, uint8_t o4)
    {
        bytes_[0] = o1;
        bytes_[1] = o2;
        bytes_[2] = o3;
        bytes_[3] = o4;
    }

    uint8_t operator[](int index) const { return bytes_[static_cast<size_t>(index) & 3]; }
    uint8_t& operator[](int index) { return bytes_[static_cast<size_t>(index) & 3]; }

    // Little-endian 32-bit representation, matching real Arduino IPAddress's
    // operator uint32_t() (byte 0 in the low-order byte) -- callers like proto_ping.ino
    // assign this straight into lwIP's ip4_addr_t.addr, which expects that layout.
    operator uint32_t() const
    {
        return static_cast<uint32_t>(bytes_[0]) |
               (static_cast<uint32_t>(bytes_[1]) << 8) |
               (static_cast<uint32_t>(bytes_[2]) << 16) |
               (static_cast<uint32_t>(bytes_[3]) << 24);
    }

    bool operator==(const IPAddress& other) const
    {
        return bytes_[0] == other.bytes_[0] && bytes_[1] == other.bytes_[1] &&
               bytes_[2] == other.bytes_[2] && bytes_[3] == other.bytes_[3];
    }
    bool operator!=(const IPAddress& other) const { return !(*this == other); }

    String toString() const
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", bytes_[0], bytes_[1], bytes_[2], bytes_[3]);
        return String(buf);
    }

private:
    uint8_t bytes_[4] = {0, 0, 0, 0};
};
