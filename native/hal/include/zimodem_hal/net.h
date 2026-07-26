#pragma once

// Internal HAL API (not Arduino-shaped). Backs WiFiClient/WiFiServer/WiFiUDP in
// zimodem_hal/arduino/WiFi.h: real BSD sockets on Linux, real Winsock on Windows --
// TCP dialing/listening and UDP (for rt_clock.ino's NTP sync) both need a genuine OS
// socket on either platform, there is no meaningful further abstraction to do here
// beyond hiding the Winsock-vs-POSIX API differences behind one shape.
//
// All read paths are non-blocking-poll style (available()/read() return immediately,
// 0 or -1 if nothing's ready) to match how zimodem's own code drives WiFiClient: it
// polls available() in its own loop() rather than blocking on read().

#include <cstddef>
#include <cstdint>
#include <string>

namespace zimodem_hal::net
{
    // Must be called once before any socket use (WSAStartup on Windows; a no-op on
    // POSIX), and global_shutdown() once at process exit. The wrapper's instance
    // lifecycle (native/wrapper) is responsible for this, not individual sockets.
    void global_init();
    void global_shutdown();

    class TcpSocket
    {
    public:
        TcpSocket();
        ~TcpSocket();
        TcpSocket(const TcpSocket&) = delete;
        TcpSocket& operator=(const TcpSocket&) = delete;
        TcpSocket(TcpSocket&& other) noexcept;
        TcpSocket& operator=(TcpSocket&& other) noexcept;

        // Resolves host (dotted IPv4 or hostname) and connects. Blocking (matches
        // WiFiClient::connect's real-hardware behavior of blocking until connected or
        // timed out) -- callers on the wrapper's dedicated background thread (see
        // docs/native-wrapper-spec.md section 7.2), so this never blocks a P/Invoke call.
        bool connect(const std::string& host, uint16_t port);

        bool is_open() const;
        // True if the socket looks connected from the local side. Real disconnect
        // detection on a non-blocking socket only happens when a read/available call
        // observes the peer closed, matching WiFiClient::connected()'s own real-hardware
        // imprecision.
        bool connected() const;

        int available() const;
        int read(uint8_t* buf, size_t len);
        int peek_byte();
        size_t write(const uint8_t* buf, size_t len);
        void set_no_delay(bool enable);
        void flush();
        void close();

        // For a connected socket: the local port this end is bound to, and the remote
        // peer's IPv4 address (network byte order, matching IPAddress(uint32_t)'s
        // documented layout) -- needed when a WiFiServer-accepted connection has to
        // record who connected (WiFiClientNode's constructor-from-WiFiClient).
        uint16_t local_port() const;
        uint32_t remote_ip() const;

        // Used by TcpListener::accept() to wrap an already-connected native handle.
        // Deliberately not public API beyond this file/net.cpp -- exposed via a factory
        // rather than a public constructor so ownership of the raw handle stays obvious.
        static TcpSocket adopt(std::intptr_t native_handle);

    private:
        std::intptr_t handle_;
        mutable int peeked_ = -1; // one-byte pushback so peek() doesn't consume

        explicit TcpSocket(std::intptr_t handle) : handle_(handle) {}
    };

    class TcpListener
    {
    public:
        TcpListener() = default;
        ~TcpListener();
        TcpListener(const TcpListener&) = delete;
        TcpListener& operator=(const TcpListener&) = delete;

        bool listen(uint16_t port);
        bool has_pending_client() const;
        TcpSocket accept(); // returns a closed/invalid TcpSocket if none pending
        void close();

    private:
        std::intptr_t handle_ = -1;
    };

    class UdpSocket
    {
    public:
        UdpSocket() = default;
        ~UdpSocket();
        UdpSocket(const UdpSocket&) = delete;
        UdpSocket& operator=(const UdpSocket&) = delete;

        bool begin(uint16_t local_port); // 0 = any available local port

        // Matches Arduino WiFiUDP's begin/write/end packet-building sequence.
        bool begin_packet(const std::string& host, uint16_t port);
        size_t write(const uint8_t* buf, size_t len);
        bool end_packet();

        // Returns the size of the next available datagram (0 if none), matching
        // WiFiUDP::parsePacket(). Must be called before read().
        int parse_packet();
        int read(uint8_t* buf, size_t len);

        void close();

    private:
        std::intptr_t handle_ = -1;
        std::string pending_host_;
        uint16_t pending_port_ = 0;
        std::string send_buffer_;
        std::string recv_buffer_;
        size_t recv_pos_ = 0;
    };

    // Resolves a hostname or dotted-decimal IPv4 address. Returns false if resolution
    // fails. out_addr is in network byte order (matches inet/sockaddr_in usage directly).
    bool resolve_ipv4(const std::string& host, uint32_t& out_addr);
}
