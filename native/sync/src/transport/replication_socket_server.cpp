#include "transport/replication_socket_server.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/error.hpp>

#include "connection/sqlite_pool.h"
#include "sync_logging.h"
#include "sync_schema.h"
#include "text/ascii.h"
#include "time/ewma.h"

namespace tightrope::sync::transport {

namespace {

enum class ConnectionFailureReason {
    Read,
    Apply,
    TlsHandshake,
    HandshakeAck,
};

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

std::string_view error_or_default(const std::string* error, const std::string_view fallback) {
    if (error != nullptr && !error->empty()) {
        return *error;
    }
    return fallback;
}

std::string normalize_bind_host(const std::string_view raw_host) {
    const auto trimmed = core::text::trim_ascii(raw_host);
    if (trimmed.empty()) {
        return "0.0.0.0";
    }

    const auto lowered = core::text::to_lower_ascii(trimmed);
    if (lowered == "localhost") {
        return "127.0.0.1";
    }
    return std::string(trimmed);
}

std::uint64_t now_unix_ms() {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    return static_cast<std::uint64_t>(now.time_since_epoch().count());
}

std::uint64_t saturating_add_u64(const std::uint64_t lhs, const std::uint64_t rhs) {
    if (lhs > (std::numeric_limits<std::uint64_t>::max() - rhs)) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return lhs + rhs;
}

std::uint64_t connection_duration_ms(const std::chrono::steady_clock::time_point started_at) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at);
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed.count()));
}

constexpr double kConnectionDurationEwmaAlpha = 0.2;

void note_connection_duration_ewma(ReplicationSocketServerTelemetry& telemetry, const std::uint64_t duration_ms) {
    const auto observations = telemetry.completed_connections + telemetry.failed_connections;
    const std::optional<double> seed =
        observations > 1 ? std::optional<double>(telemetry.connection_duration_ewma_ms) : std::nullopt;
    core::time::Ewma<double> ewma{kConnectionDurationEwmaAlpha, seed};
    telemetry.connection_duration_ewma_ms = ewma.update(static_cast<double>(duration_ms));
}

void note_connection_duration_histogram(ReplicationSocketServerTelemetry& telemetry, const std::uint64_t duration_ms) {
    if (duration_ms <= 10) {
        ++telemetry.connection_duration_le_10ms;
    } else if (duration_ms <= 50) {
        ++telemetry.connection_duration_le_50ms;
    } else if (duration_ms <= 250) {
        ++telemetry.connection_duration_le_250ms;
    } else if (duration_ms <= 1000) {
        ++telemetry.connection_duration_le_1000ms;
    } else {
        ++telemetry.connection_duration_gt_1000ms;
    }
}

struct IngressDrainTelemetry {
    std::uint64_t max_buffered_bytes = 0;
    std::uint64_t max_queued_frames = 0;
    std::uint64_t max_queued_payload_bytes = 0;
    std::uint64_t paused_read_cycles = 0;
    std::uint64_t paused_read_sleep_ms = 0;
};

void note_ingress_saturation(IngressDrainTelemetry& telemetry, const ReplicationIngressSession& session) {
    telemetry.max_buffered_bytes =
        std::max(telemetry.max_buffered_bytes, static_cast<std::uint64_t>(session.buffered_bytes()));
    telemetry.max_queued_frames =
        std::max(telemetry.max_queued_frames, static_cast<std::uint64_t>(session.pending_frames()));
    telemetry.max_queued_payload_bytes =
        std::max(telemetry.max_queued_payload_bytes, static_cast<std::uint64_t>(session.pending_payload_bytes()));
}

bool drain_pending_frames(
    ReplicationIngressSession& session,
    const std::uint64_t pause_sleep_ms,
    const std::atomic<bool>& stop_requested,
    IngressDrainTelemetry* telemetry,
    std::string* error
) {
    if (telemetry != nullptr) {
        note_ingress_saturation(*telemetry, session);
    }
    while (session.has_pending_frames()) {
        const auto drained = session.drain();
        if (!drained.ok) {
            set_error(error, drained.error);
            return false;
        }
        if (telemetry != nullptr) {
            note_ingress_saturation(*telemetry, session);
        }
        if (!drained.pause_reads || drained.resume_reads) {
            continue;
        }
        if (telemetry != nullptr) {
            ++telemetry->paused_read_cycles;
            telemetry->paused_read_sleep_ms = saturating_add_u64(telemetry->paused_read_sleep_ms, pause_sleep_ms);
        }
        if (pause_sleep_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pause_sleep_ms));
        }
        if (stop_requested.load(std::memory_order_acquire)) {
            return true;
        }
    }
    return true;
}

} // namespace

ReplicationSocketServer::ReplicationSocketServer(std::string db_path, ReplicationSocketServerConfig config)
    : db_path_(std::move(db_path)), config_(std::move(config)) {}

ReplicationSocketServer::~ReplicationSocketServer() {
    stop();
}

bool ReplicationSocketServer::start(std::string* error) {
    if (db_path_.empty()) {
        set_error(error, "replication ingress requires a non-empty db path");
        return false;
    }
    if (running_.load(std::memory_order_acquire)) {
        return true;
    }

    stop_requested_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_.clear();
        bound_port_ = 0;
        telemetry_ = {};
    }

    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread([this] { run(); });

    for (int attempt = 0; attempt < 200; ++attempt) {
        if (running_.load(std::memory_order_acquire)) {
            return true;
        }
        if (!last_error().empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto message = last_error().empty() ? std::string("replication ingress listener failed to start") : last_error();
    set_error(error, message);
    stop();
    return false;
}

void ReplicationSocketServer::stop() {
    stop_requested_.store(true, std::memory_order_release);

    std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        acceptor = acceptor_;
    }
    if (acceptor != nullptr) {
        boost::system::error_code ec;
        acceptor->cancel(ec);
        acceptor->close(ec);
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        acceptor_.reset();
        io_context_.reset();
        running_.store(false, std::memory_order_release);
    }
}

bool ReplicationSocketServer::is_running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

std::uint16_t ReplicationSocketServer::bound_port() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return bound_port_;
}

std::string ReplicationSocketServer::last_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

ReplicationSocketServerTelemetry ReplicationSocketServer::telemetry() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return telemetry_;
}

void ReplicationSocketServer::set_last_error(std::string message) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = std::move(message);
}

bool ReplicationSocketServer::configure_acceptor(boost::asio::ip::tcp::acceptor& acceptor, std::string* error) {
    const auto host = normalize_bind_host(config_.bind_host);
    boost::system::error_code ec;
    const auto address = boost::asio::ip::make_address(host, ec);
    if (ec) {
        set_error(error, "invalid replication ingress bind host '" + host + "': " + ec.message());
        return false;
    }

    boost::asio::ip::tcp::endpoint endpoint(address, config_.bind_port);
    acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        set_error(error, "failed to open replication ingress acceptor: " + ec.message());
        return false;
    }

    acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
    if (ec) {
        set_error(error, "failed to set replication ingress reuse_address: " + ec.message());
        return false;
    }

    acceptor.bind(endpoint, ec);
    if (ec) {
        set_error(error, "failed to bind replication ingress listener: " + ec.message());
        return false;
    }

    acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        set_error(error, "failed to listen on replication ingress listener: " + ec.message());
        return false;
    }

    const auto bound = acceptor.local_endpoint(ec);
    if (ec) {
        set_error(error, "failed to resolve replication ingress bound endpoint: " + ec.message());
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bound_port_ = bound.port();
    }
    return true;
}

bool ReplicationSocketServer::handle_plain_connection(sqlite3* db, boost::asio::ip::tcp::socket& socket, std::string* error) {
    ReplicationIngressSession session(db, config_.apply_request, config_.ingress);
    std::vector<std::uint8_t> read_buffer(std::max<std::size_t>(config_.read_chunk_bytes, 1U));
    const auto started_at = std::chrono::steady_clock::now();
    std::uint64_t connection_bytes_read = 0;
    IngressDrainTelemetry ingress_telemetry{};
    auto mark_complete = [this, &connection_bytes_read, &ingress_telemetry, started_at]() {
        const auto duration_ms = connection_duration_ms(started_at);
        std::lock_guard<std::mutex> lock(mutex_);
        ++telemetry_.completed_connections;
        telemetry_.bytes_read = saturating_add_u64(telemetry_.bytes_read, connection_bytes_read);
        telemetry_.total_connection_duration_ms =
            saturating_add_u64(telemetry_.total_connection_duration_ms, duration_ms);
        telemetry_.last_connection_duration_ms = duration_ms;
        telemetry_.max_connection_duration_ms = std::max(telemetry_.max_connection_duration_ms, duration_ms);
        note_connection_duration_ewma(telemetry_, duration_ms);
        note_connection_duration_histogram(telemetry_, duration_ms);
        telemetry_.max_buffered_bytes = std::max(telemetry_.max_buffered_bytes, ingress_telemetry.max_buffered_bytes);
        telemetry_.max_queued_frames = std::max(telemetry_.max_queued_frames, ingress_telemetry.max_queued_frames);
        telemetry_.max_queued_payload_bytes =
            std::max(telemetry_.max_queued_payload_bytes, ingress_telemetry.max_queued_payload_bytes);
        telemetry_.paused_read_cycles =
            saturating_add_u64(telemetry_.paused_read_cycles, ingress_telemetry.paused_read_cycles);
        telemetry_.paused_read_sleep_ms =
            saturating_add_u64(telemetry_.paused_read_sleep_ms, ingress_telemetry.paused_read_sleep_ms);
        if (telemetry_.active_connections > 0) {
            --telemetry_.active_connections;
        }
    };
    auto mark_failure = [this, &connection_bytes_read, &ingress_telemetry, started_at](const ConnectionFailureReason reason, const std::string_view message) {
        const auto duration_ms = connection_duration_ms(started_at);
        std::lock_guard<std::mutex> lock(mutex_);
        ++telemetry_.failed_connections;
        telemetry_.bytes_read = saturating_add_u64(telemetry_.bytes_read, connection_bytes_read);
        telemetry_.total_connection_duration_ms =
            saturating_add_u64(telemetry_.total_connection_duration_ms, duration_ms);
        telemetry_.last_connection_duration_ms = duration_ms;
        telemetry_.max_connection_duration_ms = std::max(telemetry_.max_connection_duration_ms, duration_ms);
        note_connection_duration_ewma(telemetry_, duration_ms);
        note_connection_duration_histogram(telemetry_, duration_ms);
        telemetry_.max_buffered_bytes = std::max(telemetry_.max_buffered_bytes, ingress_telemetry.max_buffered_bytes);
        telemetry_.max_queued_frames = std::max(telemetry_.max_queued_frames, ingress_telemetry.max_queued_frames);
        telemetry_.max_queued_payload_bytes =
            std::max(telemetry_.max_queued_payload_bytes, ingress_telemetry.max_queued_payload_bytes);
        telemetry_.paused_read_cycles =
            saturating_add_u64(telemetry_.paused_read_cycles, ingress_telemetry.paused_read_cycles);
        telemetry_.paused_read_sleep_ms =
            saturating_add_u64(telemetry_.paused_read_sleep_ms, ingress_telemetry.paused_read_sleep_ms);
        telemetry_.last_failure_at_unix_ms = now_unix_ms();
        telemetry_.last_failure_error = std::string(message);
        if (telemetry_.active_connections > 0) {
            --telemetry_.active_connections;
        }
        switch (reason) {
        case ConnectionFailureReason::Read:
            ++telemetry_.read_failures;
            break;
        case ConnectionFailureReason::Apply:
            ++telemetry_.apply_failures;
            break;
        case ConnectionFailureReason::TlsHandshake:
            ++telemetry_.tls_handshake_failures;
            break;
        case ConnectionFailureReason::HandshakeAck:
            ++telemetry_.handshake_ack_failures;
            break;
        }
    };

    note_ingress_saturation(ingress_telemetry, session);
    while (!stop_requested_.load(std::memory_order_acquire)) {
        if (!drain_pending_frames(session, config_.paused_drain_sleep_ms, stop_requested_, &ingress_telemetry, error)) {
            mark_failure(ConnectionFailureReason::Apply, error_or_default(error, "replication ingress drain failed"));
            return false;
        }

        boost::system::error_code ec;
        const auto read = socket.read_some(boost::asio::buffer(read_buffer), ec);
        if (ec == boost::asio::error::eof || ec == boost::asio::error::connection_reset) {
            mark_complete();
            return true;
        }
        if (ec) {
            set_error(error, "replication ingress read failed: " + ec.message());
            mark_failure(ConnectionFailureReason::Read, error_or_default(error, "replication ingress read failed"));
            return false;
        }
        if (read == 0) {
            continue;
        }
        connection_bytes_read += static_cast<std::uint64_t>(read);

        const auto outcome =
            session.ingest(std::span<const std::uint8_t>(read_buffer.data(), static_cast<std::size_t>(read)));
        note_ingress_saturation(ingress_telemetry, session);
        if (!outcome.ok) {
            set_error(error, "replication ingress apply failed: " + outcome.error);
            mark_failure(ConnectionFailureReason::Apply, error_or_default(error, "replication ingress apply failed"));
            return false;
        }
        if (outcome.handshake_accepted) {
            const auto ack = RpcChannel::encode({
                .channel = config_.ingress.handshake_channel,
                .payload = std::vector<std::uint8_t>{1},
            });
            boost::system::error_code write_ec;
            (void)boost::asio::write(socket, boost::asio::buffer(ack), write_ec);
            if (write_ec == boost::asio::error::broken_pipe || write_ec == boost::asio::error::connection_reset) {
                mark_complete();
                return true;
            }
            if (write_ec) {
                set_error(error, "replication ingress handshake ack failed: " + write_ec.message());
                mark_failure(
                    ConnectionFailureReason::HandshakeAck,
                    error_or_default(error, "replication ingress handshake ack failed"));
                return false;
            }
        }
    }

    mark_complete();
    return true;
}

bool ReplicationSocketServer::handle_tls_connection(sqlite3* db, boost::asio::ip::tcp::socket socket, std::string* error) {
    const auto started_at = std::chrono::steady_clock::now();
    std::uint64_t connection_bytes_read = 0;
    IngressDrainTelemetry ingress_telemetry{};
    auto mark_complete = [this, &connection_bytes_read, &ingress_telemetry, started_at]() {
        const auto duration_ms = connection_duration_ms(started_at);
        std::lock_guard<std::mutex> lock(mutex_);
        ++telemetry_.completed_connections;
        telemetry_.bytes_read = saturating_add_u64(telemetry_.bytes_read, connection_bytes_read);
        telemetry_.total_connection_duration_ms =
            saturating_add_u64(telemetry_.total_connection_duration_ms, duration_ms);
        telemetry_.last_connection_duration_ms = duration_ms;
        telemetry_.max_connection_duration_ms = std::max(telemetry_.max_connection_duration_ms, duration_ms);
        note_connection_duration_ewma(telemetry_, duration_ms);
        note_connection_duration_histogram(telemetry_, duration_ms);
        telemetry_.max_buffered_bytes = std::max(telemetry_.max_buffered_bytes, ingress_telemetry.max_buffered_bytes);
        telemetry_.max_queued_frames = std::max(telemetry_.max_queued_frames, ingress_telemetry.max_queued_frames);
        telemetry_.max_queued_payload_bytes =
            std::max(telemetry_.max_queued_payload_bytes, ingress_telemetry.max_queued_payload_bytes);
        telemetry_.paused_read_cycles =
            saturating_add_u64(telemetry_.paused_read_cycles, ingress_telemetry.paused_read_cycles);
        telemetry_.paused_read_sleep_ms =
            saturating_add_u64(telemetry_.paused_read_sleep_ms, ingress_telemetry.paused_read_sleep_ms);
        if (telemetry_.active_connections > 0) {
            --telemetry_.active_connections;
        }
    };
    auto mark_failure = [this, &connection_bytes_read, &ingress_telemetry, started_at](const ConnectionFailureReason reason, const std::string_view message) {
        const auto duration_ms = connection_duration_ms(started_at);
        std::lock_guard<std::mutex> lock(mutex_);
        ++telemetry_.failed_connections;
        telemetry_.bytes_read = saturating_add_u64(telemetry_.bytes_read, connection_bytes_read);
        telemetry_.total_connection_duration_ms =
            saturating_add_u64(telemetry_.total_connection_duration_ms, duration_ms);
        telemetry_.last_connection_duration_ms = duration_ms;
        telemetry_.max_connection_duration_ms = std::max(telemetry_.max_connection_duration_ms, duration_ms);
        note_connection_duration_ewma(telemetry_, duration_ms);
        note_connection_duration_histogram(telemetry_, duration_ms);
        telemetry_.max_buffered_bytes = std::max(telemetry_.max_buffered_bytes, ingress_telemetry.max_buffered_bytes);
        telemetry_.max_queued_frames = std::max(telemetry_.max_queued_frames, ingress_telemetry.max_queued_frames);
        telemetry_.max_queued_payload_bytes =
            std::max(telemetry_.max_queued_payload_bytes, ingress_telemetry.max_queued_payload_bytes);
        telemetry_.paused_read_cycles =
            saturating_add_u64(telemetry_.paused_read_cycles, ingress_telemetry.paused_read_cycles);
        telemetry_.paused_read_sleep_ms =
            saturating_add_u64(telemetry_.paused_read_sleep_ms, ingress_telemetry.paused_read_sleep_ms);
        telemetry_.last_failure_at_unix_ms = now_unix_ms();
        telemetry_.last_failure_error = std::string(message);
        if (telemetry_.active_connections > 0) {
            --telemetry_.active_connections;
        }
        switch (reason) {
        case ConnectionFailureReason::Read:
            ++telemetry_.read_failures;
            break;
        case ConnectionFailureReason::Apply:
            ++telemetry_.apply_failures;
            break;
        case ConnectionFailureReason::TlsHandshake:
            ++telemetry_.tls_handshake_failures;
            break;
        case ConnectionFailureReason::HandshakeAck:
            ++telemetry_.handshake_ack_failures;
            break;
        }
    };

    std::shared_ptr<boost::asio::io_context> io_context;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        io_context = io_context_;
    }
    if (io_context == nullptr) {
        set_error(error, "replication ingress io context is unavailable");
        mark_failure(ConnectionFailureReason::TlsHandshake, error_or_default(error, "replication ingress io context is unavailable"));
        return false;
    }

    TlsStream tls_stream(*io_context, true);
    std::string tls_error;
    if (!tls_stream.configure(config_.tls, &tls_error)) {
        set_error(error, "replication ingress TLS configure failed: " + tls_error);
        mark_failure(ConnectionFailureReason::TlsHandshake, error_or_default(error, "replication ingress TLS configure failed"));
        return false;
    }
    tls_stream.socket() = std::move(socket);
    if (!tls_stream.handshake_server(&tls_error)) {
        set_error(error, "replication ingress TLS handshake failed: " + tls_error);
        mark_failure(ConnectionFailureReason::TlsHandshake, error_or_default(error, "replication ingress TLS handshake failed"));
        return false;
    }

    ReplicationIngressSession session(db, config_.apply_request, config_.ingress);
    std::vector<std::uint8_t> read_buffer(std::max<std::size_t>(config_.read_chunk_bytes, 1U));
    note_ingress_saturation(ingress_telemetry, session);

    while (!stop_requested_.load(std::memory_order_acquire)) {
        if (!drain_pending_frames(session, config_.paused_drain_sleep_ms, stop_requested_, &ingress_telemetry, error)) {
            mark_failure(ConnectionFailureReason::Apply, error_or_default(error, "replication ingress drain failed"));
            return false;
        }

        boost::system::error_code ec;
        const auto read = tls_stream.stream().read_some(boost::asio::buffer(read_buffer), ec);
        if (ec == boost::asio::error::eof || ec == boost::asio::ssl::error::stream_truncated) {
            mark_complete();
            return true;
        }
        if (ec) {
            set_error(error, "replication ingress TLS read failed: " + ec.message());
            mark_failure(ConnectionFailureReason::Read, error_or_default(error, "replication ingress TLS read failed"));
            return false;
        }
        if (read == 0) {
            continue;
        }
        connection_bytes_read += static_cast<std::uint64_t>(read);

        const auto outcome =
            session.ingest(std::span<const std::uint8_t>(read_buffer.data(), static_cast<std::size_t>(read)));
        note_ingress_saturation(ingress_telemetry, session);
        if (!outcome.ok) {
            set_error(error, "replication ingress apply failed: " + outcome.error);
            mark_failure(ConnectionFailureReason::Apply, error_or_default(error, "replication ingress apply failed"));
            return false;
        }
        if (outcome.handshake_accepted) {
            const auto ack = RpcChannel::encode({
                .channel = config_.ingress.handshake_channel,
                .payload = std::vector<std::uint8_t>{1},
            });
            boost::system::error_code write_ec;
            (void)boost::asio::write(tls_stream.stream(), boost::asio::buffer(ack), write_ec);
            if (write_ec == boost::asio::ssl::error::stream_truncated || write_ec == boost::asio::error::eof ||
                write_ec == boost::asio::error::connection_reset) {
                mark_complete();
                return true;
            }
            if (write_ec) {
                set_error(error, "replication ingress handshake ack failed: " + write_ec.message());
                mark_failure(
                    ConnectionFailureReason::HandshakeAck,
                    error_or_default(error, "replication ingress handshake ack failed"));
                return false;
            }
        }
    }

    mark_complete();
    return true;
}

void ReplicationSocketServer::run() {
    db::SqlitePool db_pool(db_path_);
    if (!db_pool.open() || db_pool.connection() == nullptr) {
        set_last_error("replication ingress database open failed");
        return;
    }
    if (!sync::ensure_sync_schema(db_pool.connection())) {
        db_pool.close();
        set_last_error("replication ingress schema init failed");
        return;
    }

    auto io_context = std::make_shared<boost::asio::io_context>();
    auto acceptor = std::make_shared<boost::asio::ip::tcp::acceptor>(*io_context);

    std::string acceptor_error;
    if (!configure_acceptor(*acceptor, &acceptor_error)) {
        set_last_error(std::move(acceptor_error));
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        io_context_ = io_context;
        acceptor_ = acceptor;
    }
    running_.store(true, std::memory_order_release);
    log_sync_event(
        SyncLogLevel::Info,
        "replication_socket_server",
        "listener_started",
        "host=" + normalize_bind_host(config_.bind_host) + " port=" + std::to_string(bound_port()) +
            " tls=" + std::string(config_.tls_enabled ? "1" : "0"));

    while (!stop_requested_.load(std::memory_order_acquire)) {
        boost::asio::ip::tcp::socket socket(*io_context);
        boost::system::error_code ec;
        acceptor->accept(socket, ec);
        if (ec) {
            if (stop_requested_.load(std::memory_order_acquire) || ec == boost::asio::error::operation_aborted ||
                ec == boost::asio::error::bad_descriptor) {
                break;
            }
            const auto accept_error = "replication ingress accept failed: " + ec.message();
            set_last_error(accept_error);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++telemetry_.accept_failures;
                telemetry_.last_failure_at_unix_ms = now_unix_ms();
                telemetry_.last_failure_error = accept_error;
            }
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++telemetry_.accepted_connections;
            ++telemetry_.active_connections;
            telemetry_.peak_active_connections = std::max(telemetry_.peak_active_connections, telemetry_.active_connections);
            telemetry_.last_connection_at_unix_ms = now_unix_ms();
        }

        std::string connection_error;
        const bool ok = config_.tls_enabled
                          ? handle_tls_connection(db_pool.connection(), std::move(socket), &connection_error)
                          : handle_plain_connection(db_pool.connection(), socket, &connection_error);
        if (!ok && !connection_error.empty()) {
            set_last_error(connection_error);
            log_sync_event(
                SyncLogLevel::Warning,
                "replication_socket_server",
                "connection_failed",
                connection_error);
        }
    }

    running_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        acceptor_.reset();
        io_context_.reset();
    }
    db_pool.close();
    log_sync_event(SyncLogLevel::Info, "replication_socket_server", "listener_stopped");
}

} // namespace tightrope::sync::transport
