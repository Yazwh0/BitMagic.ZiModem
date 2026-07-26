#pragma once

// Backs the global `ESP` object real Arduino cores provide. Every call site in the parts
// of zimodem we compile lands in the "#else" (non-ESP32) branch of an existing
// `#ifdef ZIMODEM_ESP32` ladder (see zimodem.ino's setup() and zcommand.ino's
// showInitMessage()), which uses the ESP8266-shaped subset of this API: getSdkVersion(),
// getFlashChipId(), getFlashChipRealSize(), getCpuFreqMHz(), getSketchSize(). None of
// these have a meaningful host equivalent, so they return fixed, host-appropriate values
// -- there is no real "flash chip" or "SDK" on a PC. getFlashChipRealSize() specifically
// needs to be >= 4096*1024 (see zimodem.ino's `(ESP.getFlashChipRealSize()/1024)>=4096`
// check in setup()) so the host build takes the richer of the two ESP8266 pin-support
// branches -- an arbitrary but harmless choice, since those pins are virtual anyway.

#include <cstdint>

class ESPClass
{
public:
    const char* getSdkVersion() const { return "zimodem-host"; }
    uint32_t getFlashChipId() const { return 0; }
    uint32_t getFlashChipRealSize() const { return 4u * 1024u * 1024u; }
    uint32_t getFlashChipSize() const { return 4u * 1024u * 1024u; }
    uint32_t getCpuFreqMHz() const { return 240; }
    uint32_t getFreeHeap() const { return 0; }
    uint32_t getSketchSize() const { return 0; }
    uint32_t getChipRevision() const { return 0; }
};

extern ESPClass ESP;
