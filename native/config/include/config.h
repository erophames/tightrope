#pragma once

#include <cstdint>
#include <optional>
#include <string>

// Configuration struct

namespace tightrope::config {

struct Config {
    std::string host = "127.0.0.1";
    std::uint16_t port = 2455;
    std::string db_path = "store.db";
    std::string config_path;
    std::string log_level = "info";
    std::int64_t sticky_ttl_ms = 30 * 60 * 1000;
    std::int64_t sticky_cleanup_interval_ms = 60 * 1000;
};

struct ConfigOverrides {
    std::optional<std::string> host;
    std::optional<std::uint16_t> port;
    std::optional<std::string> db_path;
    std::optional<std::string> config_path;
    std::optional<std::string> log_level;
    std::optional<std::int64_t> sticky_ttl_ms;
    std::optional<std::int64_t> sticky_cleanup_interval_ms;
};

} // namespace tightrope::config
