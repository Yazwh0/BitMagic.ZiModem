#pragma once

// Matches the subset of Arduino's WiFi (WiFiClass) global used by zimodem, per
// docs/native-wrapper-spec.md section 7.5: a host PC is already networked, so this
// reports "always connected" against the host's real primary adapter address rather
// than modeling a join/leave lifecycle. begin()/disconnect()/mode()/config()/
// setHostname() are accepted no-ops -- AT commands that check "did the modem accept the
// command" keep working without pretending we can join an arbitrary SSID from here.

#include <cstdint>
#include <string>

#include "IPAddress.h"
#include "WString.h"
#include "zimodem_hal/net.h"

enum wl_status_t
{
    WL_CONNECTED = 3
};

enum wifi_mode_t
{
    WIFI_STA = 1
};

// Real value on ESP32 is WIFI_AUTH_OPEN (pet2asc.h's ZIMODEM_ESP32 branch: `#define
// ENC_TYPE_NONE WIFI_AUTH_OPEN`); host builds don't include that ESP-IDF header, so this
// is defined directly here instead as part of the WiFi-scan stub surface below.
#define ENC_TYPE_NONE 0

class WiFiClass
{
public:
    wl_status_t status() const { return WL_CONNECTED; }

    bool begin(const char* /*ssid*/, const char* /*password*/) { return true; }
    void disconnect() {}
    void mode(wifi_mode_t) {}
    bool config(IPAddress /*ip*/, IPAddress /*gateway*/, IPAddress /*subnet*/, IPAddress /*dns*/) { return true; }

    void setHostname(const char* name) { hostname_ = name ? name : ""; }
    void hostname(const char* name) { hostname_ = name ? name : ""; }

    IPAddress localIP() const { return local_ip_; }
    IPAddress gatewayIP() const { return IPAddress(192, 168, 1, 1); }
    IPAddress subnetMask() const { return IPAddress(255, 255, 255, 0); }

    bool hostByName(const char* host, IPAddress& out)
    {
        uint32_t addr;
        if (!zimodem_hal::net::resolve_ipv4(host ? host : "", addr))
            return false;
        out = IPAddress(addr);
        return true;
    }

    // AT+WIFI's network-scan listing (zcommand.ino's doWiFiCommand): there is no real
    // Wi-Fi radio to scan on a host, so this reports zero networks found rather than
    // fabricating fake ones -- consistent with the "always connected, host is already
    // networked" policy elsewhere in this shim.
    int scanNetworks() { return 0; }
    String SSID(int) { return String(""); }
    int32_t RSSI(int) { return 0; }
    int encryptionType(int) { return ENC_TYPE_NONE; }

    String macAddress() { return String("00:00:00:00:00:00"); }

private:
    // A fixed loopback placeholder for now (see docs/native-wrapper-spec.md section 12,
    // open decision #2) -- the host's real adapter address isn't detected yet. Revisit
    // once the wrapper needs to reflect real network presence/absence rather than
    // always-connected.
    IPAddress local_ip_{127, 0, 0, 1};
    std::string hostname_;
};

extern WiFiClass WiFi;
