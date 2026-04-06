#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tightrope::sync::consensus::nuraft_backend {

struct BackendOptions {
    std::uint32_t election_timeout_lower_ms = 150;
    std::uint32_t election_timeout_upper_ms = 350;
    std::uint32_t heartbeat_interval_ms = 75;
    std::uint32_t rpc_failure_backoff_ms = 50;
    std::uint32_t max_append_size = 64;
    std::uint32_t thread_pool_size = 0; // 0 means auto-size from hardware.
    bool test_mode = false;
};

struct ServerInfo {
    std::uint32_t node_id = 0;
    std::string endpoint;
};

class Backend {
public:
    Backend(std::uint32_t node_id, std::vector<std::uint32_t> members, std::uint16_t port_base,
            std::string storage_base_dir = "", BackendOptions options = {});
    ~Backend();

    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    [[nodiscard]] bool start();
    void stop();
    [[nodiscard]] bool is_running() const noexcept;
    [[nodiscard]] bool is_leader() const noexcept;
    [[nodiscard]] std::int32_t leader_id() const noexcept;
    [[nodiscard]] std::uint64_t term() const noexcept;
    [[nodiscard]] std::uint64_t committed_index() const noexcept;
    [[nodiscard]] std::uint64_t last_log_index() const noexcept;
    [[nodiscard]] std::size_t committed_entry_count() const noexcept;
    [[nodiscard]] bool trigger_election() noexcept;
    [[nodiscard]] std::optional<std::uint64_t> append_payload(std::string_view payload);
    [[nodiscard]] bool add_server(std::uint32_t node_id, std::string endpoint, std::string* error = nullptr);
    [[nodiscard]] bool remove_server(std::uint32_t node_id, std::string* error = nullptr);
    [[nodiscard]] std::vector<ServerInfo> servers() const;
    [[nodiscard]] std::vector<std::uint32_t> server_ids() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string endpoint_for(std::uint32_t node_id, std::uint16_t port_base);

} // namespace tightrope::sync::consensus::nuraft_backend
