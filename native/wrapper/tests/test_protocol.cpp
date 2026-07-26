// Protocol-level tests driving zimodem_core directly (setup()/loop() + zimodem_hal),
// same style as test_smoke.cpp: real vendored+patched code, no mocking of the sketch
// itself. Where a test needs something on "the other end of the wire" (an HTTP server,
// an FTP server, an IRC server), it spins up a tiny fake one on a background thread
// using zimodem_hal::net directly -- the sketch under test always talks real BSD/Winsock
// sockets to localhost, never a stub. This mirrors why a background thread is required
// at all: doWebGet/doFTPGet/etc. are fully synchronous, blocking functions called from
// within a single loop() call, so whatever is on the other end has to be able to make
// progress concurrently with that blocked call.
//
// NOTE: same external-network caveat as test_smoke.cpp -- rt_clock.ino fires a real NTP
// request at startup.

#include "zimodem_hal/fs_root.h"
#include "zimodem_hal/net.h"
#include "zimodem_hal/pins.h"
#include "zimodem_hal/serial_port.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <random>
#include <sstream>
#include <string>
#include <thread>

// Defined in zimodem.ino, compiled into zimodem_core.
extern void setup();
extern void loop();

namespace
{
    struct CoreTestGuard
    {
        CoreTestGuard()
        {
            zimodem_hal::serial::reset_for_testing();
            zimodem_hal::pins::reset_for_testing();
            zimodem_hal::fs::reset_to_fresh_temp_dir_for_testing();
            zimodem_hal::net::global_init();
        }
        ~CoreTestGuard() { zimodem_hal::net::global_shutdown(); }
    };

    std::string capture_output_over(int loop_iterations)
    {
        std::string out;
        zimodem_hal::serial::set_output_callback([&](const uint8_t* data, size_t len) {
            out.append(reinterpret_cast<const char*>(data), len);
        });
        for (int i = 0; i < loop_iterations; i++)
            loop();
        return out;
    }

    // Pumps loop() while appending newly-produced serial output onto `collected`
    // (rather than starting a fresh capture) -- needed whenever a test drives a
    // multi-step conversation (menus, +++ / ATH) and has to keep watching output
    // across several feed()+pump phases without losing what came before.
    template <typename Predicate>
    void pump_more(std::string& collected, Predicate done, int maxIterations = 400)
    {
        zimodem_hal::serial::set_output_callback([&](const uint8_t* data, size_t len) {
            collected.append(reinterpret_cast<const char*>(data), len);
        });
        for (int i = 0; i < maxIterations && !done(); i++)
        {
            loop();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    template <typename Predicate>
    std::string pump_until(Predicate done, int maxIterations = 400)
    {
        std::string collected;
        pump_more(collected, done, maxIterations);
        return collected;
    }

    void feed(const std::string& s)
    {
        zimodem_hal::serial::feed_input(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    // Reads a single CRLF/LF-terminated line off a fake server's control socket. Used
    // only by the fake HTTP/FTP/IRC servers below to parse what the sketch under test
    // sent them -- a small polling reader, not part of the product HAL.
    std::string read_line(zimodem_hal::net::TcpSocket& s, int timeoutMs = 5000)
    {
        std::string line;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (s.available() > 0)
            {
                uint8_t c;
                s.read(&c, 1);
                if (c == '\n')
                    return line;
                if (c != '\r')
                    line += static_cast<char>(c);
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        return line;
    }

    void write_all(zimodem_hal::net::TcpSocket& s, const std::string& data)
    {
        s.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

    bool wait_for_pending_client(zimodem_hal::net::TcpListener& listener, int timeoutMs = 5000)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (listener.has_pending_client())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }

    int checksum8(const std::string& body)
    {
        int chk8 = 0;
        for (unsigned char c : body)
        {
            chk8 += c;
            if (chk8 > 255)
                chk8 -= 256;
        }
        return chk8;
    }
}

TEST_CASE("an unrecognized AT command returns ERROR, not a silent no-op", "[protocol]")
{
    CoreTestGuard guard;
    setup();
    capture_output_over(3); // discard startup banner

    // '%' is a permanently-reserved/disabled command in the vendored firmware (see
    // zcommand.ino's switch(lastCmd), case '%': result=ZERROR unconditionally) -- a
    // deterministic way to exercise the ERROR response path without depending on any
    // network/timing state.
    feed("AT%A\r");
    std::string response = capture_output_over(5);
    REQUIRE(response == "AT%A\r\nERROR\r\n");
}

TEST_CASE("+++ escapes back to command mode without hanging up, then ATH hangs up", "[protocol]")
{
    CoreTestGuard guard;
    setup();
    capture_output_over(3);

    zimodem_hal::net::TcpListener listener;
    REQUIRE(listener.listen(19401));

    feed("ATDT127.0.0.1:19401\r");
    pump_until([&] { return listener.has_pending_client(); });
    zimodem_hal::net::TcpSocket serverSide = listener.accept();
    REQUIRE(serverSide.is_open());

    // The +++ escape requires >900ms of silence before the first '+' and >900ms of
    // silence after the third (see zimodem.ino's processPlusPlusPlus /
    // checkPlusPlusPlusEscape) -- these sleeps are that real protocol timing, not
    // arbitrary padding.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    feed("+++");
    std::string response;
    pump_more(response, [&] { return false; }, 50); // let the 3 pluses register
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    pump_more(response, [&] { return response.find("OK") != std::string::npos; });

    REQUIRE(response.find("OK") != std::string::npos);
    // Escaping to command mode must NOT hang up -- the connection is still up until an
    // explicit ATH (this is what distinguishes the +++ escape from a real disconnect).
    REQUIRE(serverSide.connected());

    feed("ATH\r");
    pump_more(response, [&] { return response.find("ATH\r\nOK\r\n") != std::string::npos; });
    REQUIRE(response.find("ATH\r\nOK\r\n") != std::string::npos);

    // The "+++" bytes themselves were forwarded to the socket before the escape was
    // recognized (see ZStream::serialIncoming -- processPlusPlusPlus() doesn't stop
    // socketWrite() from also seeing them), so they're still sitting unread in
    // serverSide's receive buffer. A closed-but-not-yet-drained socket still peeks as
    // "connected", so drain it before checking for the close.
    pump_until([&] {
        uint8_t drain[16];
        while (serverSide.available() > 0)
            serverSide.read(drain, 1);
        return !serverSide.connected();
    }, 2000);
    REQUIRE_FALSE(serverSide.connected());
}

TEST_CASE("phonebook entries survive a restart (add via IRC menu, reload picks it back up)", "[protocol]")
{
    // NOTE: the direct "ATP<num>=<mods>:<addr>,<notes>" command (ZCommand::
    // doPhonebookCommand) is NOT used to write the entry here -- it has a genuine
    // upstream bug. After splitting <mods>:<addr> at the first ':', it validates the
    // address with PhoneBookEntry::checkPhonebookEntry(), which requires the string to
    // be ALL DIGITS. Any real host:port address (dots, a colon) fails that check, so
    // ATP always returns ERROR for a normal address. See the IRC menu's own "[ADD] new
    // phonebook entry" flow below instead, which builds the PhoneBookEntry directly
    // and has no such bug -- confirmed working by the AT+irc test above. This is
    // recorded as a known firmware defect (same spirit as the IRC-echo bug already
    // documented in memory), not something silently patched.
    zimodem_hal::serial::reset_for_testing();
    zimodem_hal::pins::reset_for_testing();
    zimodem_hal::net::global_init();

    std::random_device rd;
    std::ostringstream dirName;
    dirName << "zimodem-phonebook-test-" << rd();
    std::string dataDir = (std::filesystem::temp_directory_path() / dirName.str()).string();
    zimodem_hal::fs::set_root(dataDir);

    zimodem_hal::net::TcpListener listener;
    REQUIRE(listener.listen(19406));

    setup();
    capture_output_over(3);

    feed("AT+irc\r");
    std::string response = pump_until([&] { return false; }, 30);
    feed("a\r"); // [ADD] new phonebook entry
    pump_more(response, [&] { return false; }, 20);
    feed("42\r"); // fake phone number
    pump_more(response, [&] { return false; }, 20);
    feed("127.0.0.1:19406\r"); // hostname:port
    pump_more(response, [&] { return false; }, 20);
    feed("\r"); // notes (blank)
    pump_more(response, [&] { return false; }, 20);
    feed("\r"); // options (blank -> save the entry)
    pump_more(response, [&] { return response.find("Phonebook entry added") != std::string::npos; }, 50);
    REQUIRE(response.find("Phonebook entry added") != std::string::npos);
    feed("q\r"); // back to command mode
    pump_more(response, [&] { return false; }, 20);

    // Simulate a restart: re-run setup() against the SAME data directory. setup() calls
    // PhoneBookEntry::loadPhonebook(), which clears the in-memory list and reloads it
    // from /zphonebook.txt -- so this proves persistence actually round-trips through
    // the filesystem, not just in-memory state surviving a warm process.
    setup();
    capture_output_over(3);

    feed("ATD42\r");
    std::string dialResponse = pump_until([&] { return listener.has_pending_client(); });
    REQUIRE(dialResponse.find("CONNECT") != std::string::npos);

    zimodem_hal::net::TcpSocket serverSide = listener.accept();
    REQUIRE(serverSide.is_open());

    zimodem_hal::net::global_shutdown();
}

TEST_CASE("AT&g fetches a URL over real HTTP and reports a checksummed byte count", "[protocol]")
{
    CoreTestGuard guard;
    setup();
    capture_output_over(3);

    const std::string body = "Hello, World!";

    zimodem_hal::net::TcpListener listener;
    REQUIRE(listener.listen(19402));

    std::thread server([&] {
        if (!wait_for_pending_client(listener))
            return;
        zimodem_hal::net::TcpSocket client = listener.accept();
        // Drain the request headers until the blank line -- this fake server always
        // serves the same fixed body regardless of what was actually requested.
        while (true)
        {
            std::string line = read_line(client);
            if (line.empty())
                break;
        }
        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        write_all(client, resp.str());
    });

    // AT&G's argument must be quoted (see the README: AT&G"[HOSTNAME]:[PORT]/[FILENAME]")
    // -- unquoted, the general AT-argument scanner (zcommand.ino's per-command value
    // scan for '&'-prefixed commands) stops at the very first letter it sees, since '&'
    // sub-commands normally chain short numeric args (e.g. AT&K3&G4). A scheme prefix
    // isn't part of the grammar either; parseWebUrl treats an unprefixed string as a
    // bare hostname.
    feed("AT&g\"127.0.0.1:19402/hello\"\r");
    std::string response = pump_until([&] { return false; }, 100);
    server.join();

    // headerOut()'s BTYPE_NORMAL format is "[ <channel> <size> <checksum> ]" (see
    // ZCommand::headerOut), followed immediately by the raw body bytes.
    std::ostringstream expectedHeader;
    expectedHeader << "[ 0 " << body.size() << " " << checksum8(body) << " ]";
    REQUIRE(response.find(expectedHeader.str()) != std::string::npos);
    REQUIRE(response.find(body) != std::string::npos);
}

TEST_CASE("AT&g over FTP always returns ERROR -- a confirmed upstream defect, not a test bug", "[protocol]")
{
    // doWebStream() (zcommand.ino) parses an "ftp:" URL via FTPHost::parseUrl(), which
    // mutates the argument buffer in place (inserts nulls at '@', ':', '/' to carve out
    // username/password/host/port/path) and stores the results on the FTPHost object.
    // Immediately afterward, doWebStream() UNCONDITIONALLY calls parseWebUrl() again on
    // that same (now-mutated) buffer -- a call whose result is only meant to matter for
    // the http/gopher branches. Because parseWebUrl() doesn't recognize an "ftp:"
    // prefix, and the buffer's first embedded null now leaves just "ftp:" visible from
    // the start, that second call tries to parse "ftp" as a hostname with an empty port
    // after the colon, fails, and doWebStream() returns ZERROR before ever opening a
    // socket. This reproduces with any ftp:// URL, on real firmware too -- confirmed by
    // tracing the exact buffer mutations byte-by-byte, not just observed here. The user
    // chose to leave this unpatched (same call as the IRC-echo bug -- see memory), so
    // this test documents the real, broken behavior rather than exercising a feature
    // that cannot work.
    CoreTestGuard guard;
    setup();
    capture_output_over(3);

    feed("AT&g\"ftp://user:pass@127.0.0.1:19403/whatever.txt\"\r");
    std::string response = capture_output_over(5);
    REQUIRE(response.find("ERROR") != std::string::npos);
}

TEST_CASE("AT+irc walks the phonebook-based connect menu and joins a channel over real IRC", "[protocol]")
{
    CoreTestGuard guard;
    setup();
    capture_output_over(3);

    zimodem_hal::net::TcpListener listener;
    REQUIRE(listener.listen(19405));

    std::thread server([&] {
        if (!wait_for_pending_client(listener))
            return;
        zimodem_hal::net::TcpSocket client = listener.accept();
        // Prompts the client to register (see ZIRCMode::loopMenuMode's ZIRCSTATE_WAIT
        // handling of "No Ident response").
        write_all(client, ":fake.server NOTICE AUTH :*** No Ident response\r\n");
        read_line(client); // NICK ...
        read_line(client); // USER ...
        // "376" (RPL_ENDOFMOTD) is what flips ircState from WAIT to COMMAND.
        write_all(client, ":fake.server 376 tester :End of /MOTD command.\r\n");
        read_line(client); // JOIN :#test
    });

    // AT+irc must be lowercase -- the '+' command's value string is compared with
    // strstr(vbuf,"irc") case-sensitively (see zcommand.ino), unlike a plain AT letter
    // command which gets lowercased first.
    feed("AT+irc\r");
    std::string response = pump_until([&] { return false; }, 50);

    feed("a\r"); // [ADD] new phonebook entry
    pump_more(response, [&] { return false; }, 20);
    feed("42\r"); // fake phone number
    pump_more(response, [&] { return false; }, 20);
    feed("127.0.0.1:19405\r"); // hostname:port
    pump_more(response, [&] { return false; }, 20);
    feed("\r"); // notes (blank)
    pump_more(response, [&] { return false; }, 20);
    feed("\r"); // options (blank -> save the entry)
    pump_more(response, [&] { return response.find("Phonebook entry added") != std::string::npos; }, 50);
    REQUIRE(response.find("Phonebook entry added") != std::string::npos);

    feed("42\r"); // connect to the entry just created
    pump_more(response, [&] { return response.find("Connected.") != std::string::npos; }, 100);
    REQUIRE(response.find("Connected.") != std::string::npos);

    pump_more(response, [&] { return response.find("/join") != std::string::npos; }, 200);
    REQUIRE(response.find("* Commands: /join") != std::string::npos);

    feed("/join #test\r");
    pump_more(response, [&] { return response.find("Now talking in: #test") != std::string::npos; }, 100);
    REQUIRE(response.find("* Now talking in: #test") != std::string::npos);

    server.join();
}
