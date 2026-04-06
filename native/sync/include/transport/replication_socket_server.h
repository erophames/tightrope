#pragma once
// Live replication socket ingress server (TCP/TLS + RPC queue backpressure).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <sqlite3.h>

#include "sync_engine.h"
#include "transport/replication_ingress.h"
#include "transport/tls_stream.h"

namespace tightrope::sync::transport {

struct ReplicationSocketServerConfig {
    std::string bind_host = "127.0.0.1";
    std::uint16_t bind_port = 0;
    std::uint32_t local_site_id = 0;
    std::size_t read_chunk_bytes = 16U * 1024U;
    std::uint64_t paused_drain_sleep_ms = 1;
    bool tls_enabled = false;
    TlsConfig tls{};
    sync::ApplyWireBatchRequest apply_request{};
    ReplicationIngressConfig ingress{};
};

struct ReplicationSocketServerTelemetry {
    std::uint64_t accept_failures = 0;
    std::uint64_t accepted_connections = 0;
    std::uint64_t completed_connections = 0;
    std::uint64_t failed_connections = 0;
    std::uint64_t active_connections = 0;
    std::uint64_t peak_active_connections = 0;
    std::uint64_t tls_handshake_failures = 0;
    std::uint64_t read_failures = 0;
    std::uint64_t apply_failures = 0;
    std::uint64_t handshake_ack_failures = 0;
    std::uint64_t bytes_read = 0;
    std::uint64_t total_connection_duration_ms = 0;
    std::uint64_t last_connection_duration_ms = 0;
    std::uint64_t max_connection_duration_ms = 0;
    double connection_duration_ewma_ms = 0.0;
    std::uint64_t connection_duration_le_10ms = 0;
    std::uint64_t connection_duration_le_50ms = 0;
    std::uint64_t connection_duration_le_250ms = 0;
    std::uint64_t connection_duration_le_1000ms = 0;
    std::uint64_t connection_duration_gt_1000ms = 0;
    std::uint64_t max_buffered_bytes = 0;
    std::uint64_t max_queued_frames = 0;
    std::uint64_t max_queued_payload_bytes = 0;
    std::uint64_t paused_read_cycles = 0;
    std::uint64_t paused_read_sleep_ms = 0;
    std::uint64_t last_connection_at_unix_ms = 0;
    std::uint64_t last_failure_at_unix_ms = 0;
    std::string last_failure_error;
};

class ReplicationSocketServer final {
public:
    explicit ReplicationSocketServer(std::string db_path, ReplicationSocketServerConfig config = {});
    ~ReplicationSocketServer();

    bool start(std::string* error = nullptr);
    void stop();

    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] std::uint16_t bound_port() const noexcept;
    [[nodiscard]] std::string last_error() const;
    [[nodiscard]] ReplicationSocketServerTelemetry telemetry() const;

private:
    void run();
    void set_last_error(std::string message);
    bool configure_acceptor(boost::asio::ip::tcp::acceptor& acceptor, std::string* error);
    bool handle_plain_connection(sqlite3* db, boost::asio::ip::tcp::socket socket, std::string* error);
    bool handle_tls_connection(sqlite3* db, boost::asio::ip::tcp::socket socket, std::string* error);

    std::string db_path_;
    ReplicationSocketServerConfig config_{};

    mutable std::mutex mutex_{};
    std::shared_ptr<boost::asio::io_context> io_context_{};
    std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor_{};
    std::thread worker_{};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
    std::uint16_t bound_port_ = 0;
    std::string last_error_{};
    ReplicationSocketServerTelemetry telemetry_{};
};

} // namespace tightrope::sync::transport
