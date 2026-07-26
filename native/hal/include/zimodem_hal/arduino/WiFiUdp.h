#pragma once

// Matches the subset of Arduino's WiFiUDP used by rt_clock.h/.ino for NTP time sync:
// begin(localPort), parsePacket(), read(buf,len), beginPacket(IPAddress,port),
// write(buf,len), endPacket().

#include <cstdint>
#include <string>

#include "IPAddress.h"
#include "zimodem_hal/net.h"

class WiFiUDP
{
public:
    bool begin(uint16_t localPort) { return socket_.begin(localPort); }

    bool beginPacket(IPAddress ip, uint16_t port) { return socket_.begin_packet(std::string(ip.toString().c_str()), port); }
    size_t write(const uint8_t* buf, size_t len) { return socket_.write(buf, len); }
    bool endPacket() { return socket_.end_packet(); }

    int parsePacket() { return socket_.parse_packet(); }
    int read(uint8_t* buf, size_t len) { return socket_.read(buf, static_cast<size_t>(len)); }

private:
    zimodem_hal::net::UdpSocket socket_;
};
