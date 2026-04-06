#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "p2p/dht/endpoint.h"
#include "p2p/dht/node.h"

namespace {

std::atomic<bool> g_stop_requested{false};

void handle_signal(int) {
    g_stop_requested.store(true);
}

struct Options {
    std::string bind_host = "0.0.0.0";
    std::uint16_t bind_port = 0;
    std::uint32_t network_id = 0;
    std::vector<std::string> bootstrap_addresses;
    std::optional<std::string> publish_key;
    std::optional<std::string> publish_value;
    std::uint32_t publish_ttl_seconds = 120;
    std::uint32_t publish_retry_ms = 5000;
    std::vector<std::string> lookup_keys;
    std::uint32_t interval_ms = 1000;
    std::uint32_t request_timeout_ms = 15000;
    bool use_public_defaults = true;
    bool once = false;
    bool help = false;
};

void print_usage() {
    std::cout
        << "tightrope-dht-probe\n"
        << "Local OpenDHT probe utility for discovery/publish/lookup checks.\n\n"
        << "Usage:\n"
        << "  tightrope-dht-probe [options]\n\n"
        << "Options:\n"
        << "  --bind-host <host>             Bind host (default: 0.0.0.0)\n"
        << "  --bind-port <port>             Bind port (default: 0 = auto)\n"
        << "  --network-id <id>              OpenDHT network id (default: 0 = public mainnet)\n"
        << "  --bootstrap <host:port>        Bootstrap node (repeatable)\n"
        << "  --public                       Use public defaults (network_id=0 + public seeds)\n"
        << "  --private-local                Local-only defaults (127.0.0.1 + private network id)\n"
        << "  --no-default-bootstrap         Disable automatic public bootstrap seeds\n"
        << "  --publish-key <key>            Announce key\n"
        << "  --publish-value <value>        Announce value\n"
        << "  --publish-ttl <seconds>        Announce TTL (default: 120)\n"
        << "  --publish-retry-ms <ms>        Publish retry interval (default: 5000)\n"
        << "  --lookup-key <key>             Lookup key (repeatable)\n"
        << "  --interval-ms <ms>             Status interval (default: 1000)\n"
        << "  --request-timeout-ms <ms>      DHT request timeout (default: 15000)\n"
        << "  --once                         Run one cycle and exit\n"
        << "  --help                         Show help\n";
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
        if (arg == "--bind-host") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            options->bind_host = value;
            continue;
        }
        if (arg == "--bind-port") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            const auto parsed = parse_u16(value);
            if (!parsed.has_value()) {
                std::cerr << "invalid --bind-port: " << value << '\n';
                return false;
            }
            options->bind_port = *parsed;
            continue;
        }
        if (arg == "--network-id") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            const auto parsed = parse_u32(value);
            if (!parsed.has_value()) {
                std::cerr << "invalid --network-id: " << value << '\n';
                return false;
            }
            options->network_id = *parsed;
            options->use_public_defaults = false;
            continue;
        }
        if (arg == "--bootstrap") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            options->bootstrap_addresses.push_back(value);
            options->use_public_defaults = false;
            continue;
        }
        if (arg == "--public") {
            options->bind_host = "0.0.0.0";
            options->network_id = 0;
            options->use_public_defaults = true;
            continue;
        }
        if (arg == "--private-local") {
            options->bind_host = "127.0.0.1";
            options->network_id = 0x54525050; // "TRPP"
            options->bootstrap_addresses.clear();
            options->use_public_defaults = false;
            continue;
        }
        if (arg == "--no-default-bootstrap") {
            options->use_public_defaults = false;
            continue;
        }
        if (arg == "--publish-key") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            options->publish_key = value;
            continue;
        }
        if (arg == "--publish-value") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            options->publish_value = value;
            continue;
        }
        if (arg == "--publish-ttl") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            const auto parsed = parse_u32(value);
            if (!parsed.has_value() || *parsed == 0) {
                std::cerr << "invalid --publish-ttl: " << value << '\n';
                return false;
            }
            options->publish_ttl_seconds = *parsed;
            continue;
        }
        if (arg == "--publish-retry-ms") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            const auto parsed = parse_u32(value);
            if (!parsed.has_value() || *parsed == 0) {
                std::cerr << "invalid --publish-retry-ms: " << value << '\n';
                return false;
            }
            options->publish_retry_ms = *parsed;
            continue;
        }
        if (arg == "--lookup-key") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            options->lookup_keys.push_back(value);
            continue;
        }
        if (arg == "--interval-ms") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            const auto parsed = parse_u32(value);
            if (!parsed.has_value() || *parsed == 0) {
                std::cerr << "invalid --interval-ms: " << value << '\n';
                return false;
            }
            options->interval_ms = *parsed;
            continue;
        }
        if (arg == "--request-timeout-ms") {
            if (!require_next_value(argc, argv, &i, &value, arg)) {
                return false;
            }
            const auto parsed = parse_u32(value);
            if (!parsed.has_value() || *parsed == 0) {
                std::cerr << "invalid --request-timeout-ms: " << value << '\n';
                return false;
            }
            options->request_timeout_ms = *parsed;
            continue;
        }
        if (arg == "--once") {
            options->once = true;
            continue;
        }

        std::cerr << "unknown argument: " << arg << '\n';
        return false;
    }

    if (options->publish_key.has_value() != options->publish_value.has_value()) {
        std::cerr << "--publish-key and --publish-value must be provided together\n";
        return false;
    }

    if (options->lookup_keys.empty() && options->publish_key.has_value()) {
        options->lookup_keys.push_back(*options->publish_key);
    }
    return true;
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

    if (options.use_public_defaults && options.bootstrap_addresses.empty()) {
        options.bootstrap_addresses.push_back("bootstrap.jami.net:4222");
    }

    std::vector<tightrope::p2p::dht::NodeEndpoint> bootstrap_nodes;
    bootstrap_nodes.reserve(options.bootstrap_addresses.size());
    for (const auto& raw : options.bootstrap_addresses) {
        const auto endpoint = tightrope::p2p::dht::parse_endpoint(raw);
        if (!endpoint.has_value()) {
            std::cerr << "invalid --bootstrap endpoint: " << raw << '\n';
            return 2;
        }
        bootstrap_nodes.push_back(*endpoint);
    }

    tightrope::p2p::dht::DhtNode node({
        .bind_host = options.bind_host,
        .bind_port = options.bind_port,
        .request_timeout_ms = options.request_timeout_ms,
        .bootstrap_nodes = bootstrap_nodes,
        .network_id = options.network_id,
        .peer_discovery = false,
        .public_stable = options.network_id == 0 && options.bind_host != "127.0.0.1" &&
                         options.bind_host != "localhost",
        .client_mode = false,
    });

    std::string error;
    if (!node.start(&error)) {
        std::cerr << "start failed: " << error << '\n';
        return 1;
    }

    std::cout << "node started id=" << node.node_id().to_hex()
              << " bound_port=" << node.bound_port()
              << " network_id=" << options.network_id
              << " bind_host=" << options.bind_host << '\n';

    if (!options.bootstrap_addresses.empty()) {
        std::cout << "bootstrap seeds:\n";
        for (const auto& seed : options.bootstrap_addresses) {
            std::cout << "  - " << seed << '\n';
        }
    } else {
        std::cout << "bootstrap seeds: none (standalone unless peers are added)\n";
    }

    if (!bootstrap_nodes.empty()) {
        if (!node.bootstrap(&error)) {
            std::cerr << "bootstrap failed: " << error << '\n';
            node.stop();
            return 1;
        }
        std::cout << "bootstrap complete peers=" << bootstrap_nodes.size() << '\n';
    }

    auto publish_once = [&]() -> bool {
        if (!options.publish_key.has_value()) {
            return true;
        }
        error.clear();
        const bool ok = node.announce_local(
            *options.publish_key,
            *options.publish_value,
            options.publish_ttl_seconds,
            &error
        );
        if (!ok) {
            std::cerr << "publish failed (will retry): " << error << '\n';
            return false;
        }
        std::cout << "published key=" << *options.publish_key
                  << " ttl=" << options.publish_ttl_seconds
                  << " value=" << *options.publish_value << '\n';
        return true;
    };

    const auto retry_interval = std::chrono::milliseconds(options.publish_retry_ms);
    auto next_publish_at = std::chrono::steady_clock::now();
    bool had_initial_publish_success = publish_once();
    if (!had_initial_publish_success) {
        next_publish_at = std::chrono::steady_clock::now() + retry_interval;
    }

    do {
        if (options.publish_key.has_value() && std::chrono::steady_clock::now() >= next_publish_at) {
            const bool ok = publish_once();
            next_publish_at = std::chrono::steady_clock::now() + retry_interval;
            if (ok) {
                had_initial_publish_success = true;
            }
        }

        const auto peers = node.known_nodes();
        std::cout << "status peers=" << peers.size() << " stored_values=" << node.stored_value_count() << '\n';
        for (const auto& peer : peers) {
            std::cout << "  peer id=" << peer.id.to_hex().substr(0, 12)
                      << " endpoint=" << tightrope::p2p::dht::endpoint_to_string(peer.endpoint)
                      << " verified=" << (peer.verified ? "yes" : "no") << '\n';
        }

        for (const auto& key : options.lookup_keys) {
            const auto values = node.find_value(key, 10, &error);
            std::cout << "  lookup key=" << key << " values=" << values.size() << '\n';
            for (const auto& entry : values) {
                std::cout << "    value=" << entry.value << " expires_unix_ms=" << entry.expires_unix_ms << '\n';
            }
        }

        if (options.once) {
            if (options.publish_key.has_value() && !had_initial_publish_success) {
                node.stop();
                return 1;
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
    } while (!g_stop_requested.load());

    node.stop();
    std::cout << "stopped\n";
    return 0;
}
