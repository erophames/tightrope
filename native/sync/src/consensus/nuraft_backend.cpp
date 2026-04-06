#include "consensus/nuraft_backend.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "consensus/internal/nuraft_backend_components.h"
#include "consensus/logging.h"

namespace tightrope::sync::consensus::nuraft_backend {

class Backend::Impl {
public:
    Impl(std::uint32_t node_id, std::vector<std::uint32_t> members, std::uint16_t port_base,
         std::string storage_base_dir, BackendOptions options)
        : node_id_(node_id),
          members_(internal::normalize_members(std::move(members))),
          port_base_(port_base),
          storage_path_(internal::make_storage_path(storage_base_dir, node_id, port_base)),
          options_(std::move(options)) {}

    std::uint32_t node_id_ = 0;
    std::vector<std::uint32_t> members_;
    std::uint16_t port_base_ = 26000;
    std::string storage_path_;
    BackendOptions options_{};
    bool running_ = false;

    nuraft::raft_launcher launcher_;
    nuraft::ptr<nuraft::raft_server> raft_server_;
    nuraft::ptr<internal::InMemoryStateMachine> state_machine_;
    nuraft::ptr<internal::SqliteStateManager> state_manager_;
    std::shared_ptr<internal::SqliteRaftStorage> storage_;
    nuraft::ptr<internal::NoopLogger> logger_;
};

namespace {

std::string members_to_string(const std::vector<std::uint32_t>& members) {
    std::string out;
    out.push_back('[');
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (index > 0) {
            out.push_back(',');
        }
        out.append(std::to_string(members[index]));
    }
    out.push_back(']');
    return out;
}

BackendOptions sanitize_backend_options(BackendOptions options) {
    options.election_timeout_lower_ms = std::max(options.election_timeout_lower_ms, 1u);
    options.election_timeout_upper_ms = std::max(options.election_timeout_upper_ms, options.election_timeout_lower_ms);
    options.heartbeat_interval_ms = std::max(options.heartbeat_interval_ms, 1u);
    options.rpc_failure_backoff_ms = std::max(options.rpc_failure_backoff_ms, 1u);
    options.max_append_size = std::max(options.max_append_size, 1u);
    if (options.thread_pool_size == 0) {
        const auto hardware = std::thread::hardware_concurrency();
        options.thread_pool_size = hardware == 0 ? 1u : std::min(hardware, 4u);
    }
    options.thread_pool_size = std::max(options.thread_pool_size, 1u);
    return options;
}

} // namespace

Backend::Backend(const std::uint32_t node_id, std::vector<std::uint32_t> members, const std::uint16_t port_base,
                 std::string storage_base_dir, BackendOptions options)
    : impl_(std::make_unique<Impl>(
          node_id,
          std::move(members),
          port_base,
          std::move(storage_base_dir),
          sanitize_backend_options(std::move(options)))) {}

Backend::~Backend() {
    stop();
}

bool Backend::start() {
    if (impl_->running_) {
        return true;
    }

    log_consensus_event(
        ConsensusLogLevel::Debug,
        "nuraft_backend",
        "start_begin",
        "node=" + std::to_string(impl_->node_id_) + " members=" + members_to_string(impl_->members_) +
            " port_base=" + std::to_string(impl_->port_base_) + " storage=" + impl_->storage_path_);

    const auto config = internal::build_cluster_config(impl_->members_, impl_->port_base_);
    impl_->storage_ = std::make_shared<internal::SqliteRaftStorage>(impl_->storage_path_);
    if (!impl_->storage_->open()) {
        log_consensus_event(
            ConsensusLogLevel::Error,
            "nuraft_backend",
            "start_failed_storage_open",
            "node=" + std::to_string(impl_->node_id_) + " path=" + impl_->storage_path_ +
                " error=" + impl_->storage_->last_error());
        impl_->storage_.reset();
        return false;
    }

    auto log_store = nuraft::cs_new<internal::SqliteLogStore>(impl_->storage_);
    impl_->state_machine_ = nuraft::cs_new<internal::InMemoryStateMachine>(config, impl_->storage_);
    impl_->state_manager_ =
        nuraft::cs_new<internal::SqliteStateManager>(impl_->node_id_, config, log_store, impl_->storage_);
    impl_->logger_ = nuraft::cs_new<internal::NoopLogger>();

    nuraft::raft_params params;
    params.with_election_timeout_lower(impl_->options_.election_timeout_lower_ms)
        .with_election_timeout_upper(impl_->options_.election_timeout_upper_ms)
        .with_hb_interval(impl_->options_.heartbeat_interval_ms)
        .with_rpc_failure_backoff(impl_->options_.rpc_failure_backoff_ms)
        .with_max_append_size(impl_->options_.max_append_size);
    // Keep membership reconfiguration non-blocking when peers are introduced before
    // they have fully joined; NuRaft will track them as new joiners until caught up.
    params.use_new_joiner_type_ = true;

    nuraft::asio_service::options asio_options;
    asio_options.thread_pool_size_ = impl_->options_.thread_pool_size;

    nuraft::raft_server::init_options init_options;
    init_options.skip_initial_election_timeout_ = false;
    init_options.start_server_in_constructor_ = true;
    init_options.test_mode_flag_ = impl_->options_.test_mode;

    const auto port = static_cast<int>(impl_->port_base_ + impl_->node_id_);
    try {
        impl_->raft_server_ = impl_->launcher_.init(
            impl_->state_machine_,
            impl_->state_manager_,
            impl_->logger_,
            port,
            asio_options,
            params,
            init_options
        );
    } catch (const std::exception& ex) {
        log_consensus_event(
            ConsensusLogLevel::Error,
            "nuraft_backend",
            "start_failed_launcher_exception",
            "node=" + std::to_string(impl_->node_id_) + " port=" + std::to_string(port) + " error=" + ex.what());
        impl_->raft_server_.reset();
    } catch (...) {
        log_consensus_event(
            ConsensusLogLevel::Error,
            "nuraft_backend",
            "start_failed_launcher_exception",
            "node=" + std::to_string(impl_->node_id_) + " port=" + std::to_string(port) + " error=unknown");
        impl_->raft_server_.reset();
    }

    if (!impl_->raft_server_) {
        log_consensus_event(
            ConsensusLogLevel::Error,
            "nuraft_backend",
            "start_failed_launcher_init",
            "node=" + std::to_string(impl_->node_id_) + " port=" + std::to_string(port) + " members=" +
                members_to_string(impl_->members_));
        impl_->state_machine_.reset();
        impl_->state_manager_.reset();
        impl_->logger_.reset();
        impl_->storage_.reset();
        return false;
    }

    impl_->running_ = true;
    log_consensus_event(
        ConsensusLogLevel::Info,
        "nuraft_backend",
        "started",
        "node=" + std::to_string(impl_->node_id_) + " port=" + std::to_string(port));
    return true;
}

void Backend::stop() {
    if (!impl_->running_) {
        return;
    }
    log_consensus_event(ConsensusLogLevel::Debug, "nuraft_backend", "stop_begin", "node=" + std::to_string(impl_->node_id_));
    impl_->launcher_.shutdown(3);
    impl_->raft_server_.reset();
    impl_->state_machine_.reset();
    impl_->state_manager_.reset();
    impl_->logger_.reset();
    impl_->storage_.reset();
    impl_->running_ = false;
    log_consensus_event(ConsensusLogLevel::Debug, "nuraft_backend", "stopped", "node=" + std::to_string(impl_->node_id_));
}

bool Backend::is_running() const noexcept {
    return impl_->running_ && impl_->raft_server_ != nullptr;
}

bool Backend::is_leader() const noexcept {
    return is_running() && impl_->raft_server_->is_leader();
}

std::int32_t Backend::leader_id() const noexcept {
    return is_running() ? impl_->raft_server_->get_leader() : -1;
}

std::uint64_t Backend::term() const noexcept {
    return is_running() ? impl_->raft_server_->get_term() : 0;
}

std::uint64_t Backend::committed_index() const noexcept {
    return is_running() ? impl_->raft_server_->get_committed_log_idx() : 0;
}

std::uint64_t Backend::last_log_index() const noexcept {
    return is_running() ? impl_->raft_server_->get_last_log_idx() : 0;
}

std::size_t Backend::committed_entry_count() const noexcept {
    return impl_->state_machine_ ? impl_->state_machine_->committed_entry_count() : 0;
}

bool Backend::trigger_election() noexcept {
    if (!is_running()) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "nuraft_backend",
            "trigger_election_rejected_not_running",
            "node=" + std::to_string(impl_->node_id_));
        return false;
    }
    impl_->raft_server_->restart_election_timer();
    log_consensus_event(
        ConsensusLogLevel::Debug,
        "nuraft_backend",
        "trigger_election",
        "node=" + std::to_string(impl_->node_id_) + " term=" + std::to_string(impl_->raft_server_->get_term()));
    return true;
}

std::optional<std::uint64_t> Backend::append_payload(const std::string_view payload) {
    if (!is_running()) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "nuraft_backend",
            "append_payload_rejected_not_running",
            "node=" + std::to_string(impl_->node_id_));
        return std::nullopt;
    }
    if (!impl_->raft_server_->is_leader()) {
        log_consensus_event(
            ConsensusLogLevel::Debug,
            "nuraft_backend",
            "append_payload_rejected_not_leader",
            "node=" + std::to_string(impl_->node_id_) + " leader=" + std::to_string(impl_->raft_server_->get_leader()));
        return std::nullopt;
    }
    if (payload.empty()) {
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "nuraft_backend",
            "append_payload_rejected_empty_payload",
            "node=" + std::to_string(impl_->node_id_));
        return std::nullopt;
    }

    auto data = nuraft::buffer::alloc(payload.size());
    data->put_raw(reinterpret_cast<const nuraft::byte*>(payload.data()), payload.size());
    data->pos(0);

    std::vector<nuraft::ptr<nuraft::buffer>> logs;
    logs.push_back(data);
    auto result = impl_->raft_server_->append_entries(logs);
    if (!result || result->get_result_code() != nuraft::cmd_result_code::OK || !result->get_accepted()) {
        const auto code = result ? static_cast<int>(result->get_result_code()) : -1;
        const auto accepted = result ? result->get_accepted() : false;
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "nuraft_backend",
            "append_payload_failed",
            "node=" + std::to_string(impl_->node_id_) + " result_code=" + std::to_string(code) + " accepted=" +
                std::string(accepted ? "1" : "0") + " payload_bytes=" + std::to_string(payload.size()));
        return std::nullopt;
    }
    const auto index = impl_->raft_server_->get_last_log_idx();
    log_consensus_event(
        ConsensusLogLevel::Debug,
        "nuraft_backend",
        "append_payload_accepted",
        "node=" + std::to_string(impl_->node_id_) + " index=" + std::to_string(index) + " payload_bytes=" +
            std::to_string(payload.size()));
    return index;
}

bool Backend::add_server(const std::uint32_t node_id, std::string endpoint, std::string* error) {
    auto set_error = [&error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
    };

    if (!is_running()) {
        set_error("raft backend is not running");
        return false;
    }
    if (!impl_->raft_server_->is_leader()) {
        set_error("raft node is not leader");
        return false;
    }
    if (node_id == 0 || endpoint.empty()) {
        set_error("invalid membership change request");
        return false;
    }

    nuraft::srv_config server_to_add(static_cast<nuraft::int32>(node_id), endpoint);
    auto result = impl_->raft_server_->add_srv(server_to_add);
    if (!result || !result->get_accepted()) {
        const auto detail = result ? result->get_result_str() : std::string("no result");
        if (detail.find("already exists") != std::string::npos || detail.find("Already exists") != std::string::npos) {
            auto members = impl_->members_;
            members.push_back(node_id);
            impl_->members_ = internal::normalize_members(std::move(members));
            log_consensus_event(
                ConsensusLogLevel::Info,
                "nuraft_backend",
                "add_server_noop_already_exists",
                "node=" + std::to_string(impl_->node_id_) + " target=" + std::to_string(node_id) + " endpoint=" +
                    endpoint);
            return true;
        }
        set_error("raft add_srv rejected: " + detail);
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "nuraft_backend",
            "add_server_rejected",
            "node=" + std::to_string(impl_->node_id_) + " target=" + std::to_string(node_id) + " endpoint=" +
                endpoint + " detail=" + detail);
        return false;
    }
    const auto code = result->get_result_code();
    if (code != nuraft::cmd_result_code::OK && code != nuraft::cmd_result_code::RESULT_NOT_EXIST_YET) {
        const auto detail = result->get_result_str();
        set_error("raft add_srv failed: " + detail);
        return false;
    }

    {
        auto members = impl_->members_;
        members.push_back(node_id);
        impl_->members_ = internal::normalize_members(std::move(members));
    }
    log_consensus_event(
        ConsensusLogLevel::Info,
        "nuraft_backend",
        "add_server_accepted",
        "node=" + std::to_string(impl_->node_id_) + " target=" + std::to_string(node_id) + " endpoint=" + endpoint);
    return true;
}

bool Backend::remove_server(const std::uint32_t node_id, std::string* error) {
    auto set_error = [&error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
    };

    if (!is_running()) {
        set_error("raft backend is not running");
        return false;
    }
    if (!impl_->raft_server_->is_leader()) {
        set_error("raft node is not leader");
        return false;
    }
    if (node_id == 0) {
        set_error("invalid membership change request");
        return false;
    }

    auto result = impl_->raft_server_->remove_srv(static_cast<int>(node_id));
    if (!result || !result->get_accepted()) {
        const auto detail = result ? result->get_result_str() : std::string("no result");
        if (detail.find("Cannot find server") != std::string::npos ||
            detail.find("cannot find server") != std::string::npos ||
            detail.find("not found") != std::string::npos) {
            auto members = impl_->members_;
            members.erase(
                std::remove(members.begin(), members.end(), node_id),
                members.end());
            impl_->members_ = internal::normalize_members(std::move(members));
            log_consensus_event(
                ConsensusLogLevel::Info,
                "nuraft_backend",
                "remove_server_noop_missing",
                "node=" + std::to_string(impl_->node_id_) + " target=" + std::to_string(node_id));
            return true;
        }
        set_error("raft remove_srv rejected: " + detail);
        log_consensus_event(
            ConsensusLogLevel::Warning,
            "nuraft_backend",
            "remove_server_rejected",
            "node=" + std::to_string(impl_->node_id_) + " target=" + std::to_string(node_id) + " detail=" + detail);
        return false;
    }
    const auto code = result->get_result_code();
    if (code != nuraft::cmd_result_code::OK && code != nuraft::cmd_result_code::RESULT_NOT_EXIST_YET) {
        const auto detail = result->get_result_str();
        set_error("raft remove_srv failed: " + detail);
        return false;
    }

    {
        auto members = impl_->members_;
        members.erase(
            std::remove(members.begin(), members.end(), node_id),
            members.end());
        impl_->members_ = internal::normalize_members(std::move(members));
    }
    log_consensus_event(
        ConsensusLogLevel::Info,
        "nuraft_backend",
        "remove_server_accepted",
        "node=" + std::to_string(impl_->node_id_) + " target=" + std::to_string(node_id));
    return true;
}

std::vector<ServerInfo> Backend::servers() const {
    std::vector<ServerInfo> out;
    if (!is_running()) {
        out.reserve(impl_->members_.size());
        for (const auto member_id : impl_->members_) {
            if (member_id == 0) {
                continue;
            }
            out.push_back(ServerInfo{
                .node_id = member_id,
                .endpoint = endpoint_for(member_id, impl_->port_base_),
            });
        }
        return out;
    }

    std::vector<nuraft::ptr<nuraft::srv_config>> configs;
    impl_->raft_server_->get_srv_config_all(configs);
    out.reserve(configs.size());
    for (const auto& config : configs) {
        if (config == nullptr || config->get_id() <= 0) {
            continue;
        }
        out.push_back(ServerInfo{
            .node_id = static_cast<std::uint32_t>(config->get_id()),
            .endpoint = config->get_endpoint(),
        });
    }
    std::unordered_set<std::uint32_t> seen_ids;
    seen_ids.reserve(out.size() + impl_->members_.size());
    for (const auto& server : out) {
        if (server.node_id != 0) {
            seen_ids.insert(server.node_id);
        }
    }
    for (const auto member_id : impl_->members_) {
        if (member_id == 0 || seen_ids.contains(member_id)) {
            continue;
        }
        out.push_back(ServerInfo{
            .node_id = member_id,
            .endpoint = endpoint_for(member_id, impl_->port_base_),
        });
        seen_ids.insert(member_id);
    }
    std::sort(out.begin(), out.end(), [](const ServerInfo& lhs, const ServerInfo& rhs) {
        return lhs.node_id < rhs.node_id;
    });
    out.erase(
        std::unique(
            out.begin(),
            out.end(),
            [](const ServerInfo& lhs, const ServerInfo& rhs) {
                return lhs.node_id == rhs.node_id;
            }),
        out.end());
    return out;
}

std::vector<std::uint32_t> Backend::server_ids() const {
    const auto members = servers();
    std::vector<std::uint32_t> ids;
    ids.reserve(members.size());
    for (const auto& member : members) {
        if (member.node_id == 0) {
            continue;
        }
        ids.push_back(member.node_id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::string endpoint_for(const std::uint32_t node_id, const std::uint16_t port_base) {
    return "127.0.0.1:" + std::to_string(static_cast<std::uint32_t>(port_base) + node_id);
}

} // namespace tightrope::sync::consensus::nuraft_backend
