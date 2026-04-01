#include "config_loader.h"

#include <charconv>
#include <cstdlib>
#include <limits>
#include <string_view>

#include <toml++/toml.hpp>

namespace tightrope::config {

namespace {

std::optional<std::uint16_t> parse_port_string(const std::string_view raw) {
    if (raw.empty()) {
        return std::nullopt;
    }

    unsigned parsed = 0;
    auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), parsed);
    if (ec != std::errc() || ptr != raw.data() + raw.size() || parsed > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }

    return static_cast<std::uint16_t>(parsed);
}

std::optional<std::int64_t> parse_int64_string(const std::string_view raw) {
    if (raw.empty()) {
        return std::nullopt;
    }

    std::int64_t parsed = 0;
    auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), parsed);
    if (ec != std::errc() || ptr != raw.data() + raw.size()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<std::string> read_env_string(const char* key) {
    const char* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

std::optional<std::uint16_t> read_env_port(const char* key) {
    auto raw = read_env_string(key);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return parse_port_string(*raw);
}

std::optional<std::int64_t> read_env_int64(const char* key) {
    auto raw = read_env_string(key);
    if (!raw.has_value()) {
        return std::nullopt;
    }
    return parse_int64_string(*raw);
}

std::optional<std::string> read_toml_string(const toml::node_view<const toml::node>& value) {
    if (const auto string_value = value.value<std::string>(); string_value.has_value() && !string_value->empty()) {
        return *string_value;
    }
    return std::nullopt;
}

std::optional<std::uint16_t> read_toml_port(const toml::node_view<const toml::node>& value) {
    if (const auto integer_value = value.value<std::int64_t>();
        integer_value.has_value() && *integer_value >= 0
        && *integer_value <= static_cast<std::int64_t>(std::numeric_limits<std::uint16_t>::max())) {
        return static_cast<std::uint16_t>(*integer_value);
    }

    if (const auto string_value = value.value<std::string>(); string_value.has_value()) {
        return parse_port_string(*string_value);
    }

    return std::nullopt;
}

std::optional<std::int64_t> read_toml_int64(const toml::node_view<const toml::node>& value) {
    if (const auto integer_value = value.value<std::int64_t>(); integer_value.has_value()) {
        return *integer_value;
    }
    if (const auto string_value = value.value<std::string>(); string_value.has_value()) {
        return parse_int64_string(*string_value);
    }
    return std::nullopt;
}

std::optional<toml::table> parse_toml_file(const std::string& path) {
    if (path.empty()) {
        return std::nullopt;
    }

#if TOML_EXCEPTIONS
    try {
        return toml::parse_file(path);
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }
#else
    auto parsed = toml::parse_file(path);
    if (!parsed) {
        return std::nullopt;
    }
    return std::move(parsed).table();
#endif
}

void apply_toml_file_config(const toml::table& table, Config& config) {
    if (const auto host = read_toml_string(table["host"]); host.has_value()) {
        config.host = *host;
    }
    if (const auto port = read_toml_port(table["port"]); port.has_value()) {
        config.port = *port;
    }
    if (const auto db_path = read_toml_string(table["db_path"]); db_path.has_value()) {
        config.db_path = *db_path;
    }
    if (const auto log_level = read_toml_string(table["log_level"]); log_level.has_value()) {
        config.log_level = *log_level;
    }
    if (const auto sticky_ttl_ms = read_toml_int64(table["sticky_ttl_ms"]); sticky_ttl_ms.has_value() && *sticky_ttl_ms > 0) {
        config.sticky_ttl_ms = *sticky_ttl_ms;
    }
    if (const auto sticky_cleanup_interval_ms = read_toml_int64(table["sticky_cleanup_interval_ms"]);
        sticky_cleanup_interval_ms.has_value() && *sticky_cleanup_interval_ms > 0) {
        config.sticky_cleanup_interval_ms = *sticky_cleanup_interval_ms;
    }

    if (const auto* server = table["server"].as_table(); server != nullptr) {
        if (const auto host = read_toml_string((*server)["host"]); host.has_value()) {
            config.host = *host;
        }
        if (const auto port = read_toml_port((*server)["port"]); port.has_value()) {
            config.port = *port;
        }
    }

    if (const auto* database = table["database"].as_table(); database != nullptr) {
        if (const auto db_path = read_toml_string((*database)["db_path"]); db_path.has_value()) {
            config.db_path = *db_path;
        }
        if (const auto path = read_toml_string((*database)["path"]); path.has_value()) {
            config.db_path = *path;
        }
    }

    if (const auto* logging = table["logging"].as_table(); logging != nullptr) {
        if (const auto log_level = read_toml_string((*logging)["level"]); log_level.has_value()) {
            config.log_level = *log_level;
        }
        if (const auto log_level = read_toml_string((*logging)["log_level"]); log_level.has_value()) {
            config.log_level = *log_level;
        }
    }

    if (const auto* proxy = table["proxy"].as_table(); proxy != nullptr) {
        if (const auto sticky_ttl_ms = read_toml_int64((*proxy)["sticky_ttl_ms"]); sticky_ttl_ms.has_value() && *sticky_ttl_ms > 0) {
            config.sticky_ttl_ms = *sticky_ttl_ms;
        }
        if (const auto sticky_cleanup_interval_ms = read_toml_int64((*proxy)["sticky_cleanup_interval_ms"]);
            sticky_cleanup_interval_ms.has_value() && *sticky_cleanup_interval_ms > 0) {
            config.sticky_cleanup_interval_ms = *sticky_cleanup_interval_ms;
        }
    }
}

} // namespace

Config load_config(const ConfigOverrides& overrides) {
    Config config;
    const auto env_host = read_env_string("TIGHTROPE_HOST");
    const auto env_port = read_env_port("TIGHTROPE_PORT");
    const auto env_db_path = read_env_string("TIGHTROPE_DB_PATH");
    const auto env_config_path = read_env_string("TIGHTROPE_CONFIG_PATH");
    const auto env_log_level = read_env_string("TIGHTROPE_LOG_LEVEL");
    const auto env_sticky_ttl_ms = read_env_int64("TIGHTROPE_STICKY_TTL_MS");
    const auto env_sticky_cleanup_interval_ms = read_env_int64("TIGHTROPE_STICKY_CLEANUP_INTERVAL_MS");

    if (overrides.config_path.has_value()) {
        config.config_path = *overrides.config_path;
    } else if (env_config_path.has_value()) {
        config.config_path = *env_config_path;
    }

    if (!config.config_path.empty()) {
        if (const auto parsed = parse_toml_file(config.config_path); parsed.has_value()) {
            apply_toml_file_config(*parsed, config);
        }
    }

    if (env_host.has_value()) {
        config.host = *env_host;
    }
    if (env_port.has_value()) {
        config.port = *env_port;
    }
    if (env_db_path.has_value()) {
        config.db_path = *env_db_path;
    }
    if (env_log_level.has_value()) {
        config.log_level = *env_log_level;
    }
    if (env_sticky_ttl_ms.has_value() && *env_sticky_ttl_ms > 0) {
        config.sticky_ttl_ms = *env_sticky_ttl_ms;
    }
    if (env_sticky_cleanup_interval_ms.has_value() && *env_sticky_cleanup_interval_ms > 0) {
        config.sticky_cleanup_interval_ms = *env_sticky_cleanup_interval_ms;
    }

    if (overrides.host.has_value()) {
        config.host = *overrides.host;
    }
    if (overrides.port.has_value()) {
        config.port = *overrides.port;
    }
    if (overrides.db_path.has_value()) {
        config.db_path = *overrides.db_path;
    }
    if (overrides.config_path.has_value()) {
        config.config_path = *overrides.config_path;
    }
    if (overrides.log_level.has_value()) {
        config.log_level = *overrides.log_level;
    }
    if (overrides.sticky_ttl_ms.has_value() && *overrides.sticky_ttl_ms > 0) {
        config.sticky_ttl_ms = *overrides.sticky_ttl_ms;
    }
    if (overrides.sticky_cleanup_interval_ms.has_value() && *overrides.sticky_cleanup_interval_ms > 0) {
        config.sticky_cleanup_interval_ms = *overrides.sticky_cleanup_interval_ms;
    }

    return config;
}

} // namespace tightrope::config
