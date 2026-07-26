#pragma once

// Matches the subset of Arduino's FS.h/SPIFFS.h that zimodem actually uses (verified by
// grep: phonebook.ino, zcommand.ino, wifiservernode.ino, zconfigmode.ino, zimodem.ino,
// filelog.ino, proto_http.ino, zprint.h). File derives from Stream because
// zcommand.ino's doWebDump does `File f = SPIFFS.open(...); doWebDump(&f, len, cache);`
// where doWebDump takes a `Stream*` -- so this isn't optional fidelity, the sketch relies
// on File actually being-a Stream. There is no SPI flash filesystem on a host PC: SPIFFS's
// flat absolute-path namespace is mapped onto a real directory via zimodem_hal::fs
// (see fs_root.h). SD is deliberately not implemented here -- every SD-touching line in
// the files we compile is already guarded by `#if INCLUDE_SD_SHELL`, which the host build
// disables (patches/zimodem/0001), so the SD global is never referenced by preprocessed
// output and doesn't need to exist.

#include <cstdint>
#include <cstdio>
#include <string>

#include "Stream.h"
#include "WString.h"

class File : public Stream
{
public:
    File() = default;
    File(FILE* handle, std::string path) : handle_(handle), path_(std::move(path)) {}
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& other) noexcept { *this = std::move(other); }
    File& operator=(File&& other) noexcept
    {
        if (this != &other)
        {
            close();
            handle_ = other.handle_;
            path_ = std::move(other.path_);
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~File() override { close(); }

    explicit operator bool() const { return handle_ != nullptr; }

    // Stream
    int available() override { return handle_ ? static_cast<int>(total_size() - current_pos()) : 0; }
    int read() override { return handle_ ? std::fgetc(handle_) : -1; }
    int peek() override
    {
        if (!handle_)
            return -1;
        long cur = std::ftell(handle_);
        int c = std::fgetc(handle_);
        std::fseek(handle_, cur, SEEK_SET);
        return c;
    }
    size_t write(uint8_t b) override { return (handle_ && std::fputc(b, handle_) != EOF) ? 1u : 0u; }
    void flush() override { if (handle_) std::fflush(handle_); }
    using Print::write; // keep write(buffer,size)/write(const char*) reachable directly on File too

    // File-specific extras
    int read(uint8_t* buf, size_t size)
    {
        return handle_ ? static_cast<int>(std::fread(buf, 1, size, handle_)) : 0;
    }
    String readString()
    {
        String result;
        int c;
        while ((c = read()) >= 0)
            result += static_cast<char>(c);
        return result;
    }
    void close()
    {
        if (handle_)
        {
            std::fclose(handle_);
            handle_ = nullptr;
        }
    }
    size_t size() { return handle_ ? static_cast<size_t>(total_size()) : 0; }
    String name() const { return String(path_.c_str()); }

    // print()/println()/printf() come from Print (via write()) -- no need to reimplement
    // them here against the raw FILE* directly.

private:
    FILE* handle_ = nullptr;
    std::string path_;

    long current_pos() const { return handle_ ? std::ftell(handle_) : 0; }
    long total_size() const
    {
        if (!handle_)
            return 0;
        long cur = std::ftell(handle_);
        std::fseek(handle_, 0, SEEK_END);
        long end = std::ftell(handle_);
        std::fseek(handle_, cur, SEEK_SET);
        return end;
    }
};

class FS
{
public:
    virtual ~FS() = default;
    virtual File open(const char* path, const char* mode) = 0;
    virtual bool exists(const char* path) = 0;
    virtual bool remove(const char* path) = 0;
};

struct FSInfo
{
    size_t totalBytes = 0;
    size_t usedBytes = 0;
};

class SPIFFSClass : public FS
{
public:
    bool begin(bool formatOnFail = false);
    void end();
    bool format();

    File open(const char* path, const char* mode) override;
    bool exists(const char* path) override;
    bool remove(const char* path) override;

    void info(FSInfo& out);
    size_t totalBytes();

private:
};

extern SPIFFSClass SPIFFS;

#define FILE_READ "r"
#define FILE_WRITE "w"
