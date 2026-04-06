#include "p2p/dht/node.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstring>
#include <future>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <opendht.h>

#include "p2p/dht/endpoint.h"

namespace tightrope::p2p::dht {

namespace {

std::uint64_t now_unix_ms() {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    return static_cast<std::uint64_t>(now.time_since_epoch().count());
}

NodeId node_id_from_infohash(const ::dht::InfoHash& info_hash) {
    return NodeId::hash_of(info_hash.toString());
}

::dht::InfoHash infohash_from_node_id(const NodeId& id) {
    std::array<std::uint8_t, ::dht::InfoHash::size()> bytes{};
    const auto& node_bytes = id.bytes();
    std::copy_n(node_bytes.begin(), bytes.size(), bytes.begin());
    return ::dht::InfoHash(bytes.data(), bytes.size());
}

::dht::InfoHash infohash_for_key(const std::string_view key) {
    return ::dht::InfoHash::get(key);
}

std::string encode_payload(const std::string_view value, const std::uint64_t expires_unix_ms) {
    return std::to_string(expires_unix_ms) + "\n" + std::string(value);
}

ValueRecord decode_payload(const ::dht::Value& input) {
    ValueRecord out{};
    const std::string payload(reinterpret_cast<const char*>(input.data.data()), input.data.size());
    const auto split = payload.find('\n');
    if (split == std::string::npos) {
        out.value = payload;
        out.expires_unix_ms = 0;
        return out;
    }

    std::uint64_t parsed_expiry = 0;
    const auto expiry_part = std::string_view(payload.data(), split);
    const auto* begin = expiry_part.data();
    const auto* end = expiry_part.data() + expiry_part.size();
    const auto parsed = std::from_chars(begin, end, parsed_expiry);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        out.value = payload;
        out.expires_unix_ms = 0;
        return out;
    }

    out.value = payload.substr(split + 1);
    out.expires_unix_ms = parsed_expiry;
    return out;
}

std::optional<NodeEndpoint> endpoint_from_sockaddr(const ::dht::SockAddr& addr) {
    return parse_endpoint(addr.toString());
}

} // namespace

struct DhtNode::Impl {
    mutable std::mutex mutex;
    ::dht::DhtRunner runner{};
    bool running = false;
    std::uint16_t bound_port = 0;
};

DhtNode::DhtNode(DhtNodeConfig config) : config_(std::move(config)), impl_(std::make_unique<Impl>()) {
    if (config_.max_results == 0) {
        config_.max_results = 20;
    }
    if (config_.request_timeout_ms == 0) {
        config_.request_timeout_ms = 1200;
    }
    if (config_.node_id.has_value()) {
        node_id_ = *config_.node_id;
    }
}

DhtNode::~DhtNode() {
    stop();
}

bool DhtNode::start(std::string* error) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->running) {
        return true;
    }

    try {
        ::dht::DhtRunner::Config runner_config{};
        runner_config.threaded = true;
        runner_config.peer_discovery = config_.peer_discovery;
        runner_config.dht_config.node_config.network = config_.network_id;
        runner_config.dht_config.node_config.public_stable = config_.public_stable;
        runner_config.dht_config.node_config.client_mode = config_.client_mode;
        if (config_.node_id.has_value()) {
            runner_config.dht_config.node_config.node_id = infohash_from_node_id(*config_.node_id);
        }

        const std::string service = std::to_string(config_.bind_port);
        const bool bind_any = config_.bind_host.empty() || config_.bind_host == "0.0.0.0" || config_.bind_host == "::";
        if (bind_any) {
            impl_->runner.run(config_.bind_port, runner_config);
        } else {
            const bool maybe_v6 = config_.bind_host.find(':') != std::string::npos;
            const char* ip4 = maybe_v6 ? "" : config_.bind_host.c_str();
            const char* ip6 = maybe_v6 ? config_.bind_host.c_str() : "";
            impl_->runner.run(ip4, ip6, service.c_str(), runner_config);
        }
    } catch (const std::exception& ex) {
        set_error(error, std::string("OpenDHT start failed: ") + ex.what());
        return false;
    }

    impl_->bound_port = impl_->runner.getBoundPort(AF_INET);
    if (impl_->bound_port == 0) {
        impl_->bound_port = impl_->runner.getBoundPort(AF_INET6);
    }
    node_id_ = node_id_from_infohash(impl_->runner.getNodeId());
    impl_->running = true;
    return true;
}

void DhtNode::stop() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->running) {
        return;
    }
    try {
        impl_->runner.join();
    } catch (...) {
    }
    impl_->bound_port = 0;
    impl_->running = false;
}

bool DhtNode::is_running() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->running;
}

std::uint16_t DhtNode::bound_port() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->bound_port;
}

const NodeId& DhtNode::node_id() const noexcept {
    return node_id_;
}

bool DhtNode::bootstrap(std::string* error) {
    return bootstrap(config_.bootstrap_nodes, error);
}

bool DhtNode::bootstrap(const std::vector<NodeEndpoint>& endpoints, std::string* error) {
    if (endpoints.empty()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->running) {
        set_error(error, "node is not running");
        return false;
    }

    std::vector<::dht::SockAddr> nodes;
    nodes.reserve(endpoints.size());
    for (const auto& endpoint : endpoints) {
        if (!is_valid_endpoint(endpoint)) {
            continue;
        }
        try {
            const auto resolved = ::dht::SockAddr::resolve(endpoint.host, std::to_string(endpoint.port));
            if (!resolved.empty()) {
                nodes.push_back(resolved.front());
            }
        } catch (...) {
        }
    }
    if (nodes.empty()) {
        set_error(error, "no bootstrap endpoints were resolvable");
        return false;
    }

    auto completion = std::make_shared<std::promise<bool>>();
    auto completion_fut = completion->get_future();
    auto fired = std::make_shared<std::atomic<bool>>(false);

    impl_->runner.bootstrap(nodes, [completion, fired](const bool ok) {
        if (!fired->exchange(true)) {
            completion->set_value(ok);
        }
    });

    const auto wait_result = completion_fut.wait_for(std::chrono::milliseconds(config_.request_timeout_ms));
    if (wait_result == std::future_status::ready) {
        const auto ok = completion_fut.get();
        if (!ok) {
            set_error(error, "bootstrap callback reported failure");
        }
        return ok;
    }
    set_error(error, "bootstrap request timed out");
    return false;
}

bool DhtNode::announce_local(
    std::string key,
    std::string value,
    const std::uint32_t ttl_seconds,
    std::string* error
) {
    if (key.empty() || value.empty() || ttl_seconds == 0) {
        set_error(error, "key/value/ttl must be present");
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->running) {
        set_error(error, "node is not running");
        return false;
    }

    const auto expiry = now_unix_ms() + static_cast<std::uint64_t>(ttl_seconds) * 1000ULL;
    auto payload = encode_payload(value, expiry);
    ::dht::Value dht_value(
        reinterpret_cast<const std::uint8_t*>(payload.data()),
        static_cast<std::size_t>(payload.size())
    );

    auto completion = std::make_shared<std::promise<bool>>();
    auto completion_fut = completion->get_future();
    auto fired = std::make_shared<std::atomic<bool>>(false);

    impl_->runner.put(
        infohash_for_key(key),
        std::move(dht_value),
        [completion, fired](const bool ok) {
            if (!fired->exchange(true)) {
                completion->set_value(ok);
            }
        }
    );

    const auto wait_result = completion_fut.wait_for(std::chrono::milliseconds(config_.request_timeout_ms));
    if (wait_result != std::future_status::ready) {
        set_error(error, "put request timed out");
        return false;
    }
    const auto ok = completion_fut.get();
    if (!ok) {
        set_error(error, "OpenDHT put rejected");
    }
    return ok;
}

std::vector<ValueRecord> DhtNode::find_value(const std::string_view key, const std::size_t limit, std::string* error) {
    if (key.empty() || limit == 0) {
        return {};
    }

    std::unique_lock<std::mutex> lock(impl_->mutex);
    if (!impl_->running) {
        set_error(error, "node is not running");
        return {};
    }

    auto result_future = impl_->runner.get(infohash_for_key(key));
    lock.unlock();

    const auto status = result_future.wait_for(std::chrono::milliseconds(config_.request_timeout_ms));
    if (status != std::future_status::ready) {
        set_error(error, "get request timed out");
        return {};
    }

    std::vector<ValueRecord> out;
    out.reserve(limit);
    std::set<std::string> seen_values;
    const auto now = now_unix_ms();
    for (const auto& entry : result_future.get()) {
        if (entry == nullptr) {
            continue;
        }
        const auto decoded = decode_payload(*entry);
        if (decoded.value.empty()) {
            continue;
        }
        if (decoded.expires_unix_ms != 0 && decoded.expires_unix_ms <= now) {
            continue;
        }
        if (!seen_values.insert(decoded.value).second) {
            continue;
        }
        out.push_back(decoded);
        if (out.size() >= limit) {
            break;
        }
    }
    return out;
}

std::vector<NodeContact> DhtNode::known_nodes(const std::size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->running) {
        return {};
    }

    std::vector<NodeContact> out;
    const auto exports = impl_->runner.exportNodes();
    out.reserve(exports.size());
    const auto seen_ms = now_unix_ms();
    for (const auto& item : exports) {
        const auto endpoint = endpoint_from_sockaddr(item.addr);
        if (!endpoint.has_value()) {
            continue;
        }
        const auto id = node_id_from_infohash(item.id);
        if (id == node_id_) {
            continue;
        }
        out.push_back({
            .id = id,
            .endpoint = *endpoint,
            .last_seen_unix_ms = seen_ms,
            .verified = true,
        });
    }

    if (limit > 0 && out.size() > limit) {
        out.resize(limit);
    }
    return out;
}

std::size_t DhtNode::stored_value_count() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->running) {
        return 0;
    }
    const auto [a, b] = impl_->runner.getStoreSize();
    return a + b;
}

void DhtNode::set_error(std::string* error, const std::string_view message) {
    if (error != nullptr) {
        *error = std::string(message);
    }
}

} // namespace tightrope::p2p::dht
