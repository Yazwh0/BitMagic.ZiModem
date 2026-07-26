#include "zimodem_hal/net.h"

#include <cstring>

#ifdef _WIN32
# include <winsock2.h>
# include <ws2tcpip.h>
# pragma comment(lib, "Ws2_32.lib")
typedef int socklen_t_compat;
#else
# include <arpa/inet.h>
# include <fcntl.h>
# include <netdb.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <sys/ioctl.h>
# include <sys/socket.h>
# include <unistd.h>
typedef socklen_t socklen_t_compat;
#endif

namespace zimodem_hal::net
{
    namespace
    {
#ifdef _WIN32
        using native_t = SOCKET;
        constexpr native_t kInvalid = INVALID_SOCKET;
        int close_native(native_t s) { return closesocket(s); }
        void set_non_blocking(native_t s)
        {
            u_long mode = 1;
            ioctlsocket(s, FIONBIO, &mode);
        }
        bool would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
        int bytes_available_native(native_t s)
        {
            u_long n = 0;
            return ioctlsocket(s, FIONREAD, &n) == 0 ? static_cast<int>(n) : 0;
        }
#else
        using native_t = int;
        constexpr native_t kInvalid = -1;
        int close_native(native_t s) { return ::close(s); }
        void set_non_blocking(native_t s)
        {
            int flags = fcntl(s, F_GETFL, 0);
            fcntl(s, F_SETFL, flags | O_NONBLOCK);
        }
        bool would_block() { return errno == EWOULDBLOCK || errno == EAGAIN; }
        int bytes_available_native(native_t s)
        {
            int n = 0;
            return ioctl(s, FIONREAD, &n) == 0 ? n : 0;
        }
#endif

        native_t to_native(std::intptr_t h) { return static_cast<native_t>(h); }
        std::intptr_t from_native(native_t s) { return static_cast<std::intptr_t>(s); }
    }

    void global_init()
    {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    void global_shutdown()
    {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool resolve_ipv4(const std::string& host, uint32_t& out_addr)
    {
        // Try dotted-decimal first (matches Arduino's typical direct-IP dial usage; also
        // avoids a needless DNS round trip on the ATDT "host:port" fast path).
        in_addr direct{};
        if (inet_pton(AF_INET, host.c_str(), &direct) == 1)
        {
            out_addr = direct.s_addr;
            return true;
        }

        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr)
            return false;

        auto* addr = reinterpret_cast<sockaddr_in*>(result->ai_addr);
        out_addr = addr->sin_addr.s_addr;
        freeaddrinfo(result);
        return true;
    }

    // ---------------------------------------------------------------- TcpSocket

    TcpSocket::TcpSocket() : handle_(from_native(kInvalid)) {}

    TcpSocket::~TcpSocket() { close(); }

    TcpSocket::TcpSocket(TcpSocket&& other) noexcept : handle_(other.handle_), peeked_(other.peeked_)
    {
        other.handle_ = from_native(kInvalid);
        other.peeked_ = -1;
    }

    TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept
    {
        if (this != &other)
        {
            close();
            handle_ = other.handle_;
            peeked_ = other.peeked_;
            other.handle_ = from_native(kInvalid);
            other.peeked_ = -1;
        }
        return *this;
    }

    TcpSocket TcpSocket::adopt(std::intptr_t native_handle)
    {
        return TcpSocket(native_handle);
    }

    bool TcpSocket::connect(const std::string& host, uint16_t port)
    {
        close();

        uint32_t addr;
        if (!resolve_ipv4(host, addr))
            return false;

        native_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == kInvalid)
            return false;

        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr.s_addr = addr;

        if (::connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0)
        {
            close_native(s);
            return false;
        }

        set_non_blocking(s);
        handle_ = from_native(s);
        return true;
    }

    bool TcpSocket::is_open() const { return to_native(handle_) != kInvalid; }

    bool TcpSocket::connected() const
    {
        if (!is_open())
            return false;
        // A zero-length peek that returns 0 (orderly shutdown) means the peer closed.
        uint8_t probe;
        native_t s = to_native(handle_);
#ifdef _WIN32
        int n = recv(s, reinterpret_cast<char*>(&probe), 1, MSG_PEEK);
#else
        int n = static_cast<int>(recv(s, &probe, 1, MSG_PEEK));
#endif
        if (n == 0)
            return false;
        if (n < 0 && !would_block())
            return false;
        return true;
    }

    int TcpSocket::available() const
    {
        if (!is_open())
            return 0;
        int fromSocket = bytes_available_native(to_native(handle_));
        return fromSocket + (peeked_ >= 0 ? 1 : 0);
    }

    int TcpSocket::read(uint8_t* buf, size_t len)
    {
        if (!is_open() || len == 0)
            return 0;

        size_t written = 0;
        if (peeked_ >= 0)
        {
            buf[written++] = static_cast<uint8_t>(peeked_);
            peeked_ = -1;
            if (written == len)
                return static_cast<int>(written);
        }

        native_t s = to_native(handle_);
#ifdef _WIN32
        int n = recv(s, reinterpret_cast<char*>(buf + written), static_cast<int>(len - written), 0);
#else
        int n = static_cast<int>(recv(s, buf + written, len - written, 0));
#endif
        if (n > 0)
            return static_cast<int>(written) + n;
        if (n == 0)
            return written > 0 ? static_cast<int>(written) : -1; // orderly peer close
        return would_block() ? static_cast<int>(written) : -1;
    }

    int TcpSocket::peek_byte()
    {
        if (peeked_ >= 0)
            return peeked_;
        uint8_t b;
        int n = read(&b, 1);
        if (n == 1)
        {
            peeked_ = b;
            return b;
        }
        return -1;
    }

    size_t TcpSocket::write(const uint8_t* buf, size_t len)
    {
        if (!is_open() || len == 0)
            return 0;
        native_t s = to_native(handle_);
#ifdef _WIN32
        int n = send(s, reinterpret_cast<const char*>(buf), static_cast<int>(len), 0);
#else
        int n = static_cast<int>(send(s, buf, len, 0));
#endif
        return n > 0 ? static_cast<size_t>(n) : 0;
    }

    void TcpSocket::set_no_delay(bool enable)
    {
        if (!is_open())
            return;
        int flag = enable ? 1 : 0;
        setsockopt(to_native(handle_), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
    }

    void TcpSocket::flush() { /* no user-space send buffer to flush */ }

    void TcpSocket::close()
    {
        if (is_open())
        {
            close_native(to_native(handle_));
            handle_ = from_native(kInvalid);
        }
        peeked_ = -1;
    }

    uint16_t TcpSocket::local_port() const
    {
        if (!is_open())
            return 0;
        sockaddr_in sa{};
        socklen_t_compat len = sizeof(sa);
        if (getsockname(to_native(handle_), reinterpret_cast<sockaddr*>(&sa), &len) != 0)
            return 0;
        return ntohs(sa.sin_port);
    }

    uint32_t TcpSocket::remote_ip() const
    {
        if (!is_open())
            return 0;
        sockaddr_in sa{};
        socklen_t_compat len = sizeof(sa);
        if (getpeername(to_native(handle_), reinterpret_cast<sockaddr*>(&sa), &len) != 0)
            return 0;
        return sa.sin_addr.s_addr;
    }

    // ---------------------------------------------------------------- TcpListener

    TcpListener::~TcpListener() { close(); }

    bool TcpListener::listen(uint16_t port)
    {
        close();
        native_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == kInvalid)
            return false;

        int reuse = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr.s_addr = INADDR_ANY;

        if (bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0 || ::listen(s, 8) != 0)
        {
            close_native(s);
            return false;
        }

        set_non_blocking(s);
        handle_ = from_native(s);
        return true;
    }

    bool TcpListener::has_pending_client() const
    {
        if (to_native(handle_) == kInvalid)
            return false;
        native_t s = to_native(handle_);
        fd_set set;
        FD_ZERO(&set);
        FD_SET(s, &set);
        timeval tv{0, 0};
#ifdef _WIN32
        return select(0, &set, nullptr, nullptr, &tv) > 0;
#else
        return select(s + 1, &set, nullptr, nullptr, &tv) > 0;
#endif
    }

    TcpSocket TcpListener::accept()
    {
        if (!has_pending_client())
            return TcpSocket::adopt(from_native(kInvalid));

        native_t s = ::accept(to_native(handle_), nullptr, nullptr);
        if (s == kInvalid)
            return TcpSocket::adopt(from_native(kInvalid));

        set_non_blocking(s);
        return TcpSocket::adopt(from_native(s));
    }

    void TcpListener::close()
    {
        if (to_native(handle_) != kInvalid)
        {
            close_native(to_native(handle_));
            handle_ = -1;
        }
    }

    // ---------------------------------------------------------------- UdpSocket

    UdpSocket::~UdpSocket() { close(); }

    bool UdpSocket::begin(uint16_t local_port)
    {
        close();
        native_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == kInvalid)
            return false;

        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(local_port);
        sa.sin_addr.s_addr = INADDR_ANY;
        if (bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0)
        {
            close_native(s);
            return false;
        }

        set_non_blocking(s);
        handle_ = from_native(s);
        return true;
    }

    bool UdpSocket::begin_packet(const std::string& host, uint16_t port)
    {
        pending_host_ = host;
        pending_port_ = port;
        send_buffer_.clear();
        return true;
    }

    size_t UdpSocket::write(const uint8_t* buf, size_t len)
    {
        send_buffer_.append(reinterpret_cast<const char*>(buf), len);
        return len;
    }

    bool UdpSocket::end_packet()
    {
        if (to_native(handle_) == kInvalid)
            return false;

        uint32_t addr;
        if (!resolve_ipv4(pending_host_, addr))
            return false;

        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(pending_port_);
        sa.sin_addr.s_addr = addr;

        native_t s = to_native(handle_);
#ifdef _WIN32
        int n = sendto(s, send_buffer_.data(), static_cast<int>(send_buffer_.size()), 0,
                        reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
#else
        int n = static_cast<int>(sendto(s, send_buffer_.data(), send_buffer_.size(), 0,
                                         reinterpret_cast<sockaddr*>(&sa), sizeof(sa)));
#endif
        send_buffer_.clear();
        return n >= 0;
    }

    int UdpSocket::parse_packet()
    {
        if (to_native(handle_) == kInvalid)
            return 0;

        char buf[2048];
        sockaddr_in from{};
        socklen_t_compat fromLen = sizeof(from);
        native_t s = to_native(handle_);
#ifdef _WIN32
        int n = recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
#else
        int n = static_cast<int>(recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen));
#endif
        if (n <= 0)
        {
            recv_buffer_.clear();
            recv_pos_ = 0;
            return 0;
        }
        recv_buffer_.assign(buf, static_cast<size_t>(n));
        recv_pos_ = 0;
        return n;
    }

    int UdpSocket::read(uint8_t* buf, size_t len)
    {
        size_t remaining = recv_buffer_.size() - recv_pos_;
        size_t n = len < remaining ? len : remaining;
        if (n == 0)
            return 0;
        std::memcpy(buf, recv_buffer_.data() + recv_pos_, n);
        recv_pos_ += n;
        return static_cast<int>(n);
    }

    void UdpSocket::close()
    {
        if (to_native(handle_) != kInvalid)
        {
            close_native(to_native(handle_));
            handle_ = -1;
        }
        recv_buffer_.clear();
        recv_pos_ = 0;
    }
}
