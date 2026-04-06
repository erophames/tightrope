#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "bridge.h"

namespace {

std::atomic<bool> g_stop_requested{false};

void handle_signal(int) {
    g_stop_requested.store(true);
}

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 2466;
    std::string oauth_callback_host = "localhost";
    std::uint16_t oauth_callback_port = 1456;
    std::string db_path;

    std::string cluster_name = "default";
    std::uint32_t site_id = 2;
    std::uint16_t sync_port = 9402;
    bool discovery_enabled = false;
    std::vector<std::string> manual_peers;
    bool require_handshake_auth = true;
    std::string cluster_shared_secret = "cluster-secret";
    bool tls_enabled = false;
    bool tls_verify_peer = false;
    std::uint32_t status_interval_ms = 2000;
    bool help = false;
};

void print_usage() {
    std::cout
        << "tightrope-cluster-node\n"
        << "Starts a local bridge runtime and joins a sync cluster.\n\n"
        << "Usage:\n"
        << "  tightrope-cluster-node [options]\n\n"
        << "Options:\n"
        << "  --host <host>                       Admin API bind host (default: 127.0.0.1)\n"
        << "  --port <port>                       Admin API bind port (default: 2466)\n"
        << "  --oauth-callback-host <host>        OAuth callback bind host (default: localhost)\n"
        << "  --oauth-callback-port <port>        OAuth callback bind port (default: 1456)\n"
        << "  --db-path <path>                    Node DB path (default: /tmp/tightrope-cluster-node-<site-id>.db)\n"
        << "  --cluster-name <name>               Cluster name (default: default)\n"
        << "  --site-id <id>                      Local site id (default: 2)\n"
        << "  --sync-port <port>                  Replication ingress port (default: 9402)\n"
        << "  --peer <host:port>                  Manual peer to join (repeatable)\n"
        << "  --discovery                          Enable mDNS discovery (default: off)\n"
        << "  --shared-secret <secret>             Cluster shared secret (default: cluster-secret)\n"
        << "  --require-handshake-auth             Require handshake auth (default: on)\n"
        << "  --no-require-handshake-auth          Disable handshake auth\n"
        << "  --tls-enabled                        Enable TLS transport (default: off)\n"
        << "  --tls-verify-peer                    Enable TLS peer verification (default: off)\n"
        << "  --no-tls-verify-peer                 Disable TLS peer verification\n"
        << "  --status-interval-ms <ms>            Status print interval (default: 2000)\n"
        << "  --help                               Show help\n";
}

std::optional<std::uint16_t> parse_u16(std::string_view raw) {
    if (raw.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const auto parsed = std::strtoul(std::string(raw).c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed > 65535UL) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(parsed);
}

std::optional<std::uint32_t> parse_u32(std::string_view raw) {
    if (raw.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const auto parsed = std::strtoul(std::string(raw).c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed > 0xFFFFFFFFUL) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(parsed);
}

bool require_next_value(const int argc, char** argv, int* index, std::string* value, const std::string& flag) {
    if (index == nullptr || value == nullptr || *index + 1 >= argc) {
        std::cerr << "missing value for " << flag << '\n';
        return false;
    }
    *index += 1;
    *value = argv[*index];
    return true;
}

bool parse_args(const int argc, char** argv, Options* options) {
    if (options == nullptr) {
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string value;
        if (arg == "--help") {
            options->help = true;
            return true;
        }
        if (arg == "--host") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            options->host = value;
            continue;
        }
        if (arg == "--port") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            const auto parsed = parse_u16(value);
            if (!parsed.has_value() || *parsed == 0) {
                std::cerr << "invalid --port: " << value << '\n';
                return false;
            }
            options->port = *parsed;
            continue;
        }
        if (arg == "--oauth-callback-host") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            options->oauth_callback_host = value;
            continue;
        }
        if (arg == "--oauth-callback-port") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            const auto parsed = parse_u16(value);
            if (!parsed.has_value() || *parsed == 0) {
                std::cerr << "invalid --oauth-callback-port: " << value << '\n';
                return false;
            }
            options->oauth_callback_port = *parsed;
            continue;
        }
        if (arg == "--db-path") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            options->db_path = value;
            continue;
        }
        if (arg == "--cluster-name") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            options->cluster_name = value;
            continue;
        }
        if (arg == "--site-id") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            const auto parsed = parse_u32(value);
            if (!parsed.has_value() || *parsed == 0) {
                std::cerr << "invalid --site-id: " << value << '\n';
                return false;
            }
            options->site_id = *parsed;
            continue;
        }
        if (arg == "--sync-port") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            const auto parsed = parse_u16(value);
            if (!parsed.has_value() || *parsed == 0) {
                std::cerr << "invalid --sync-port: " << value << '\n';
                return false;
            }
            options->sync_port = *parsed;
            continue;
        }
        if (arg == "--peer") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            options->manual_peers.push_back(value);
            continue;
        }
        if (arg == "--discovery") {
            options->discovery_enabled = true;
            continue;
        }
        if (arg == "--shared-secret") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            options->cluster_shared_secret = value;
            continue;
        }
        if (arg == "--require-handshake-auth") {
            options->require_handshake_auth = true;
            continue;
        }
        if (arg == "--no-require-handshake-auth") {
            options->require_handshake_auth = false;
            continue;
        }
        if (arg == "--tls-enabled") {
            options->tls_enabled = true;
            continue;
        }
        if (arg == "--tls-verify-peer") {
            options->tls_verify_peer = true;
            continue;
        }
        if (arg == "--no-tls-verify-peer") {
            options->tls_verify_peer = false;
            continue;
        }
        if (arg == "--status-interval-ms") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            const auto parsed = parse_u32(value);
            if (!parsed.has_value() || *parsed == 0) {
                std::cerr << "invalid --status-interval-ms: " << value << '\n';
                return false;
            }
            options->status_interval_ms = *parsed;
            continue;
        }

        std::cerr << "unknown argument: " << arg << '\n';
        return false;
    }

    if (options->db_path.empty()) {
        options->db_path = "/tmp/tightrope-cluster-node-" + std::to_string(options->site_id) + ".db";
    }

    return true;
}

std::string role_name(const tightrope::bridge::ClusterRole role) {
    using Role = tightrope::bridge::ClusterRole;
    switch (role) {
        case Role::Leader:
            return "leader";
        case Role::Follower:
            return "follower";
        case Role::Candidate:
            return "candidate";
        case Role::Standalone:
        default:
            return "standalone";
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_args(argc, argv, &options)) {
        print_usage();
        return 2;
    }
    if (options.help) {
        print_usage();
        return 0;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    tightrope::bridge::Bridge bridge;
    const tightrope::bridge::BridgeConfig bridge_config{
        .host = options.host,
        .port = options.port,
        .oauth_callback_host = options.oauth_callback_host,
        .oauth_callback_port = options.oauth_callback_port,
        .db_path = options.db_path,
        .config_path = "",
    };
    if (!bridge.init(bridge_config)) {
        std::cerr << "bridge init failed: " << bridge.last_error() << '\n';
        return 1;
    }

    const tightrope::bridge::ClusterConfig cluster_config{
        .cluster_name = options.cluster_name,
        .site_id = options.site_id,
        .sync_port = options.sync_port,
        .discovery_enabled = options.discovery_enabled,
        .manual_peers = options.manual_peers,
        .require_handshake_auth = options.require_handshake_auth,
        .cluster_shared_secret = options.cluster_shared_secret,
        .tls_enabled = options.tls_enabled,
        .tls_verify_peer = options.tls_verify_peer,
    };
    if (!bridge.cluster_enable(cluster_config)) {
        std::cerr << "cluster enable failed: " << bridge.last_error() << '\n';
        (void)bridge.shutdown();
        return 1;
    }

    std::cout << "node started"
              << " cluster=" << options.cluster_name
              << " site_id=" << options.site_id
              << " sync_port=" << options.sync_port
              << " db=" << options.db_path << '\n';
    if (options.manual_peers.empty()) {
        std::cout << "manual peers: (none)\n";
    } else {
        for (const auto& peer : options.manual_peers) {
            std::cout << "manual peer: " << peer << '\n';
        }
    }
    std::cout << "press Ctrl+C to stop\n";

    while (!g_stop_requested.load()) {
        const auto status = bridge.cluster_status();
        std::cout << "status role=" << role_name(status.role)
                  << " term=" << status.term
                  << " commit=" << status.commit_index
                  << " peers=" << status.peers.size();
        if (status.leader_id.has_value()) {
            std::cout << " leader=" << *status.leader_id;
        }
        std::cout << '\n';
        std::this_thread::sleep_for(std::chrono::milliseconds(options.status_interval_ms));
    }

    (void)bridge.cluster_disable();
    if (!bridge.shutdown()) {
        std::cerr << "bridge shutdown failed: " << bridge.last_error() << '\n';
        return 1;
    }
    std::cout << "stopped\n";
    return 0;
}

