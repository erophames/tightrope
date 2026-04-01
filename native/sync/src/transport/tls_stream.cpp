#include "transport/tls_stream.h"

#include <algorithm>
#include <cctype>
#include <string>

#include <boost/asio/ssl.hpp>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace tightrope::sync::transport {

namespace {

bool is_hex_character(const char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
}

std::string to_hex(const unsigned char* bytes, const std::size_t size) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(size * 2);
    for (std::size_t index = 0; index < size; ++index) {
        const auto value = bytes[index];
        out[index * 2] = kHex[(value >> 4U) & 0x0F];
        out[index * 2 + 1] = kHex[value & 0x0F];
    }
    return out;
}

} // namespace

TlsStream::TlsStream(boost::asio::io_context& io_context, const bool server_mode)
    : context_(server_mode ? boost::asio::ssl::context::tls_server : boost::asio::ssl::context::tls_client),
      stream_(io_context, context_),
      server_mode_(server_mode) {
    context_.set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2 |
                         boost::asio::ssl::context::no_sslv3);
}

boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& TlsStream::stream() {
    return stream_;
}

const boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& TlsStream::stream() const {
    return stream_;
}

boost::asio::ip::tcp::socket& TlsStream::socket() {
    return stream_.next_layer();
}

const boost::asio::ip::tcp::socket& TlsStream::socket() const {
    return stream_.next_layer();
}

std::string TlsStream::normalize_fingerprint(const std::string_view fingerprint) {
    std::string normalized;
    normalized.reserve(fingerprint.size());
    for (const auto ch : fingerprint) {
        if (ch == ':' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (normalized.size() != 64) {
        return {};
    }
    if (!std::all_of(normalized.begin(), normalized.end(), [](const char ch) { return is_hex_character(ch); })) {
        return {};
    }
    return normalized;
}

bool TlsStream::configure_verify_callback() {
    stream_.set_verify_callback(
        [this](const bool preverified, boost::asio::ssl::verify_context& context) {
            if (!preverified) {
                return false;
            }
            if (pinned_peer_certificate_sha256_.empty()) {
                return true;
            }
            auto* store = context.native_handle();
            if (store == nullptr) {
                return false;
            }
            if (X509_STORE_CTX_get_error_depth(store) != 0) {
                return true;
            }
            X509* cert = X509_STORE_CTX_get_current_cert(store);
            if (cert == nullptr) {
                return false;
            }
            unsigned char digest[EVP_MAX_MD_SIZE] = {};
            unsigned int digest_size = 0;
            if (X509_digest(cert, EVP_sha256(), digest, &digest_size) != 1) {
                return false;
            }
            const auto observed = to_hex(digest, digest_size);
            return observed == pinned_peer_certificate_sha256_;
        }
    );
    return true;
}

bool TlsStream::configure(const TlsConfig& config, std::string* error) {
    auto set_error = [&error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
    };

    boost::system::error_code ec;
    if (!config.ca_certificate_path.empty()) {
        context_.load_verify_file(config.ca_certificate_path, ec);
    } else if (config.verify_peer) {
        context_.set_default_verify_paths(ec);
    }
    if (ec) {
        set_error("TLS CA configuration failed: " + ec.message());
        return false;
    }

    if (!config.certificate_chain_path.empty()) {
        context_.use_certificate_chain_file(config.certificate_chain_path, ec);
        if (ec) {
            set_error("TLS certificate chain load failed: " + ec.message());
            return false;
        }
    }

    if (!config.private_key_path.empty()) {
        context_.use_private_key_file(config.private_key_path, boost::asio::ssl::context::pem, ec);
        if (ec) {
            set_error("TLS private key load failed: " + ec.message());
            return false;
        }
    }

    pinned_peer_certificate_sha256_ = normalize_fingerprint(config.pinned_peer_certificate_sha256);
    if (!config.pinned_peer_certificate_sha256.empty() && pinned_peer_certificate_sha256_.empty()) {
        set_error("TLS pinned peer fingerprint must be a 64-character SHA-256 hex string");
        return false;
    }

    boost::asio::ssl::verify_mode verify_mode = boost::asio::ssl::verify_none;
    if (config.verify_peer) {
        verify_mode = boost::asio::ssl::verify_peer;
        if (server_mode_) {
            verify_mode = static_cast<boost::asio::ssl::verify_mode>(
                verify_mode | boost::asio::ssl::verify_fail_if_no_peer_cert);
        }
    }
    stream_.set_verify_mode(verify_mode);
    return configure_verify_callback();
}

bool TlsStream::handshake_client(const std::string_view server_name, std::string* error) {
    auto set_error = [&error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
    };

    if (!server_name.empty() && !SSL_set_tlsext_host_name(stream_.native_handle(), std::string(server_name).c_str())) {
        set_error("TLS SNI host configuration failed");
        return false;
    }
    boost::system::error_code ec;
    stream_.handshake(boost::asio::ssl::stream_base::client, ec);
    if (ec) {
        set_error("TLS client handshake failed: " + ec.message());
        return false;
    }
    return true;
}

bool TlsStream::handshake_server(std::string* error) {
    auto set_error = [&error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
    };
    boost::system::error_code ec;
    stream_.handshake(boost::asio::ssl::stream_base::server, ec);
    if (ec) {
        set_error("TLS server handshake failed: " + ec.message());
        return false;
    }
    return true;
}

bool TlsStream::is_open() const {
    return stream_.next_layer().is_open();
}

void TlsStream::close() {
    if (!stream_.next_layer().is_open()) {
        return;
    }
    boost::system::error_code ec;
    stream_.shutdown(ec);
    stream_.next_layer().close(ec);
}

} // namespace tightrope::sync::transport
