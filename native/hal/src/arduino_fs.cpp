#include "zimodem_hal/arduino/FS.h"
#include "zimodem_hal/fs_root.h"

#include <filesystem>

namespace stdfs = std::filesystem;

namespace
{
    // Real fopen()/CRT text-mode translation would rewrite '\n' <-> "\r\n" on Windows,
    // silently corrupting zimodem's own byte-for-byte framing (e.g. rt_clock.h writes
    // "\r\n" explicitly; phonebook/config lines are comma-delimited text zimodem parses
    // itself). Always opening in binary mode makes host behavior match the ESP8266/ESP32
    // SPIFFS implementations, which have no such translation either.
    std::string to_binary_fopen_mode(const char* mode)
    {
        std::string m = mode ? mode : "r";
        if (m.find('b') == std::string::npos)
            m += "b";
        return m;
    }
}

bool SPIFFSClass::begin(bool /*formatOnFail*/)
{
    zimodem_hal::fs::root(); // ensures the root directory exists
    return true;
}

void SPIFFSClass::end() {}

bool SPIFFSClass::format()
{
    std::error_code ec;
    for (const auto& entry : stdfs::directory_iterator(zimodem_hal::fs::root(), ec))
        stdfs::remove_all(entry.path(), ec);
    return !ec;
}

File SPIFFSClass::open(const char* path, const char* mode)
{
    std::string hostPath = zimodem_hal::fs::resolve(path ? path : "");
    FILE* handle = std::fopen(hostPath.c_str(), to_binary_fopen_mode(mode).c_str());
    return File(handle, path ? path : "");
}

bool SPIFFSClass::exists(const char* path)
{
    std::error_code ec;
    return stdfs::exists(zimodem_hal::fs::resolve(path ? path : ""), ec);
}

bool SPIFFSClass::remove(const char* path)
{
    std::error_code ec;
    return stdfs::remove(zimodem_hal::fs::resolve(path ? path : ""), ec);
}

void SPIFFSClass::info(FSInfo& out)
{
    out.totalBytes = totalBytes();
    out.usedBytes = 0;
    for (const auto& entry : stdfs::recursive_directory_iterator(zimodem_hal::fs::root()))
        if (entry.is_regular_file())
            out.usedBytes += entry.file_size();
}

size_t SPIFFSClass::totalBytes()
{
    // No real flash chip to size against on a host; report a fixed, generous capacity so
    // zimodem's "fsize=Xk"-style diagnostics (zcommand.ino's showInitMessage) print
    // something sane rather than a meaningless real disk-free number.
    return 4u * 1024u * 1024u;
}

SPIFFSClass SPIFFS;
