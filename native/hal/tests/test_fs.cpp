#include "zimodem_hal/arduino/FS.h"
#include "zimodem_hal/fs_root.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct FreshRoot
    {
        FreshRoot() { zimodem_hal::fs::reset_to_fresh_temp_dir_for_testing(); }
    };
}

TEST_CASE("exists() is false for a file that was never created", "[fs]")
{
    FreshRoot guard;
    REQUIRE_FALSE(SPIFFS.exists("/zphonebook.txt"));
}

TEST_CASE("open for write then read round-trips content, matching phonebook.ino's usage", "[fs]")
{
    FreshRoot guard;
    {
        File f = SPIFFS.open("/zphonebook.txt", "w");
        REQUIRE(static_cast<bool>(f));
        f.printf("%d,%s,%s,%s\n", 5551234, "127.0.0.1:6400", "TE", "notes");
        f.close();
    }

    REQUIRE(SPIFFS.exists("/zphonebook.txt"));

    File f = SPIFFS.open("/zphonebook.txt", "r");
    REQUIRE(static_cast<bool>(f));
    std::string line;
    int c;
    while ((c = f.read()) >= 0 && c != '\n')
        line += static_cast<char>(c);
    REQUIRE(line == "5551234,127.0.0.1:6400,TE,notes");
}

TEST_CASE("File derives from Stream: passing &f as Stream* works, matching zcommand.ino's doWebDump(&f,...)", "[fs]")
{
    FreshRoot guard;
    {
        File f = SPIFFS.open("/data.bin", "w");
        f.write(static_cast<uint8_t>('A'));
        f.write(static_cast<uint8_t>('B'));
        f.close();
    }

    File f = SPIFFS.open("/data.bin", "r");
    Stream* stream = &f;
    REQUIRE(stream->available() == 2);
    REQUIRE(stream->read() == 'A');
    REQUIRE(stream->read() == 'B');
    REQUIRE(stream->available() == 0);
}

TEST_CASE("remove deletes the file", "[fs]")
{
    FreshRoot guard;
    SPIFFS.open("/zlisteners.txt", "w").close();
    REQUIRE(SPIFFS.exists("/zlisteners.txt"));
    SPIFFS.remove("/zlisteners.txt");
    REQUIRE_FALSE(SPIFFS.exists("/zlisteners.txt"));
}

TEST_CASE("format() clears every file under the SPIFFS root", "[fs]")
{
    FreshRoot guard;
    SPIFFS.open("/a.txt", "w").close();
    SPIFFS.open("/b.txt", "w").close();
    REQUIRE(SPIFFS.exists("/a.txt"));

    SPIFFS.format();

    REQUIRE_FALSE(SPIFFS.exists("/a.txt"));
    REQUIRE_FALSE(SPIFFS.exists("/b.txt"));
}

TEST_CASE("size() reports the byte length of the file", "[fs]")
{
    FreshRoot guard;
    {
        File f = SPIFFS.open("/logfile.txt", "w");
        f.print("hello");
        f.close();
    }
    File f = SPIFFS.open("/logfile.txt", "r");
    REQUIRE(f.size() == 5);
}

TEST_CASE("readString reads all remaining bytes from the current position", "[fs]")
{
    FreshRoot guard;
    {
        File f = SPIFFS.open("/logfile.txt", "w");
        f.println("first line");
        f.print("second");
        f.close();
    }
    File f = SPIFFS.open("/logfile.txt", "r");
    String all = f.readString();
    REQUIRE(std::string(all.c_str()) == "first line\r\nsecond");
}

TEST_CASE("opening a nonexistent file for read yields an invalid (falsy) File", "[fs]")
{
    FreshRoot guard;
    File f = SPIFFS.open("/nope.txt", "r");
    REQUIRE_FALSE(static_cast<bool>(f));
}

TEST_CASE("SPIFFS.info reports usedBytes reflecting written content", "[fs]")
{
    FreshRoot guard;
    SPIFFS.open("/x.txt", "w").close();
    {
        File f = SPIFFS.open("/x.txt", "w");
        f.print("12345");
        f.close();
    }
    FSInfo info;
    SPIFFS.info(info);
    REQUIRE(info.usedBytes == 5);
    REQUIRE(info.totalBytes > 0);
}
