#include "zimodem_hal/arduino/IPAddress.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("default IPAddress is 0.0.0.0", "[ipaddress]")
{
    IPAddress ip;
    REQUIRE(ip[0] == 0);
    REQUIRE(ip[1] == 0);
    REQUIRE(ip[2] == 0);
    REQUIRE(ip[3] == 0);
    REQUIRE(std::string(ip.toString().c_str()) == "0.0.0.0");
}

TEST_CASE("four-octet constructor and operator[] round-trip, matching connSettings.ino's IPtoStr", "[ipaddress]")
{
    IPAddress ip(192, 168, 1, 50);
    REQUIRE(ip[0] == 192);
    REQUIRE(ip[1] == 168);
    REQUIRE(ip[2] == 1);
    REQUIRE(ip[3] == 50);
    REQUIRE(std::string(ip.toString().c_str()) == "192.168.1.50");
}

TEST_CASE("operator[] can mutate an octet in place", "[ipaddress]")
{
    IPAddress ip(10, 0, 0, 1);
    ip[3] = 2;
    REQUIRE(std::string(ip.toString().c_str()) == "10.0.0.2");
}

TEST_CASE("equality compares all four octets", "[ipaddress]")
{
    REQUIRE(IPAddress(1, 2, 3, 4) == IPAddress(1, 2, 3, 4));
    REQUIRE(IPAddress(1, 2, 3, 4) != IPAddress(1, 2, 3, 5));
}

TEST_CASE("parseIP-style round trip: octets parsed from a dotted string reconstruct the same toString()", "[ipaddress]")
{
    // Mirrors ConnSettings::parseIP's `new IPAddress(dots[0],dots[1],dots[2],dots[3])`.
    uint8_t dots[4] = {255, 255, 255, 0};
    IPAddress ip(dots[0], dots[1], dots[2], dots[3]);
    REQUIRE(std::string(ip.toString().c_str()) == "255.255.255.0");
}
