#include "zimodem_hal/tls.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h> // MBEDTLS_ERR_NET_* constants only
#include <mbedtls/ssl.h>

namespace zimodem_hal::net
{
    struct TlsSocketImpl
    {
        TcpSocket tcp;
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config conf;
        mbedtls_ctr_drbg_context drbg;
        mbedtls_entropy_context entropy;

        std::vector<uint8_t> rx; // decrypted-but-not-yet-consumed application bytes
        std::size_t rxpos = 0;
        bool handshaked = false;
        bool fatal = false; // peer close_notify, EOF, or an unrecoverable TLS error

        TlsSocketImpl()
        {
            mbedtls_ssl_init(&ssl);
            mbedtls_ssl_config_init(&conf);
            mbedtls_ctr_drbg_init(&drbg);
            mbedtls_entropy_init(&entropy);
        }

        ~TlsSocketImpl()
        {
            mbedtls_ssl_free(&ssl);
            mbedtls_ssl_config_free(&conf);
            mbedtls_ctr_drbg_free(&drbg);
            mbedtls_entropy_free(&entropy);
        }
    };

    namespace
    {
        // mbedTLS BIO glue over TcpSocket. TcpSocket is non-blocking: read() returns >0
        // for data, 0 when nothing is ready yet, -1 on peer-close/error; write() returns
        // the count sent or 0 when the send buffer is momentarily full. Translate the
        // "nothing yet" case into WANT_READ/WANT_WRITE (only while the socket still looks
        // connected) so mbedTLS keeps polling instead of treating it as failure.
        int bio_send(void* ctx, const unsigned char* buf, std::size_t len)
        {
            auto* tcp = static_cast<TcpSocket*>(ctx);
            std::size_t n = tcp->write(buf, len);
            if (n > 0)
                return static_cast<int>(n);
            return tcp->connected() ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
        }

        int bio_recv(void* ctx, unsigned char* buf, std::size_t len)
        {
            auto* tcp = static_cast<TcpSocket*>(ctx);
            int n = tcp->read(buf, len);
            if (n > 0)
                return n;
            if (n < 0)
                return MBEDTLS_ERR_NET_RECV_FAILED; // peer closed / socket error
            return tcp->connected() ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
        }

        // Refill rx with one TLS record's worth of application data if it's empty.
        // Never blocks: a WANT_READ/WANT_WRITE just leaves the buffer empty for now.
        void pump(TlsSocketImpl& s)
        {
            if (s.rxpos < s.rx.size() || s.fatal)
                return;

            s.rx.clear();
            s.rxpos = 0;

            unsigned char tmp[1536];
            int n = mbedtls_ssl_read(&s.ssl, tmp, sizeof(tmp));
            if (n > 0)
            {
                s.rx.assign(tmp, tmp + n);
                return;
            }
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE)
                return; // nothing ready this poll
            // 0 (unexpected EOF), PEER_CLOSE_NOTIFY, or any real error: session is done.
            s.fatal = true;
        }
    }

    TlsSocket::TlsSocket() : impl_(std::make_unique<TlsSocketImpl>()) {}

    TlsSocket::~TlsSocket() { close(); }

    bool TlsSocket::connect(const std::string& host, uint16_t port)
    {
        if (!impl_->tcp.connect(host, port))
            return false;

        static const char* kPers = "zimodem_hal_tls";
        if (mbedtls_ctr_drbg_seed(&impl_->drbg, mbedtls_entropy_func, &impl_->entropy,
                                  reinterpret_cast<const unsigned char*>(kPers),
                                  std::strlen(kPers)) != 0)
        {
            impl_->tcp.close();
            return false;
        }

        if (mbedtls_ssl_config_defaults(&impl_->conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        {
            impl_->tcp.close();
            return false;
        }

        // Insecure by design -- see zimodem_hal/tls.h. The sketch always calls
        // setInsecure(); we never receive a CA/cert to verify against.
        mbedtls_ssl_conf_authmode(&impl_->conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&impl_->conf, mbedtls_ctr_drbg_random, &impl_->drbg);

        if (mbedtls_ssl_setup(&impl_->ssl, &impl_->conf) != 0)
        {
            impl_->tcp.close();
            return false;
        }

        // SNI -- still worth setting even without verification: most HTTPS vhosts need it
        // to return the right certificate/response. Best effort.
        mbedtls_ssl_set_hostname(&impl_->ssl, host.c_str());
        mbedtls_ssl_set_bio(&impl_->ssl, &impl_->tcp, bio_send, bio_recv, nullptr);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        int ret;
        while ((ret = mbedtls_ssl_handshake(&impl_->ssl)) != 0)
        {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                impl_->tcp.close();
                return false;
            }
            if (std::chrono::steady_clock::now() > deadline)
            {
                impl_->tcp.close();
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        impl_->handshaked = true;
        return true;
    }

    bool TlsSocket::connected() const
    {
        return impl_->handshaked && !impl_->fatal && impl_->tcp.connected();
    }

    int TlsSocket::available()
    {
        if (!impl_->handshaked)
            return 0;
        pump(*impl_);
        return static_cast<int>(impl_->rx.size() - impl_->rxpos);
    }

    int TlsSocket::read(uint8_t* buf, std::size_t len)
    {
        if (!impl_->handshaked || len == 0)
            return 0;
        pump(*impl_);
        const std::size_t avail = impl_->rx.size() - impl_->rxpos;
        if (avail == 0)
            return impl_->fatal ? -1 : 0;
        const std::size_t n = std::min(len, avail);
        std::memcpy(buf, impl_->rx.data() + impl_->rxpos, n);
        impl_->rxpos += n;
        return static_cast<int>(n);
    }

    int TlsSocket::peek_byte()
    {
        if (!impl_->handshaked)
            return -1;
        pump(*impl_);
        if (impl_->rxpos < impl_->rx.size())
            return impl_->rx[impl_->rxpos];
        return -1;
    }

    size_t TlsSocket::write(const uint8_t* buf, std::size_t len)
    {
        if (!impl_->handshaked || len == 0)
            return 0;

        std::size_t sent = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (sent < len)
        {
            int n = mbedtls_ssl_write(&impl_->ssl, buf + sent, len - sent);
            if (n > 0)
            {
                sent += static_cast<std::size_t>(n);
                continue;
            }
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                if (std::chrono::steady_clock::now() > deadline)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            impl_->fatal = true;
            break;
        }
        return sent;
    }

    void TlsSocket::set_no_delay(bool enable) { impl_->tcp.set_no_delay(enable); }

    void TlsSocket::flush()
    {
        // mbedtls_ssl_write hands each record straight to the BIO (TcpSocket), which has
        // no user-space send buffer of its own -- nothing left to flush.
    }

    void TlsSocket::close()
    {
        if (impl_->handshaked && !impl_->fatal)
            mbedtls_ssl_close_notify(&impl_->ssl); // best effort
        impl_->tcp.close();
        impl_->handshaked = false;
        impl_->fatal = true;
        impl_->rx.clear();
        impl_->rxpos = 0;
    }

    uint16_t TlsSocket::local_port() const { return impl_->tcp.local_port(); }

    uint32_t TlsSocket::remote_ip() const { return impl_->tcp.remote_ip(); }
}
