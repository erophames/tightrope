#include "sync_engine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "checksum.h"
#include "conflict_resolver.h"
#include "journal_batch_id.h"
#include "sync_logging.h"
#include "time/clock.h"
#include "time/ewma.h"

namespace tightrope::sync {

namespace {

constexpr std::size_t kDefaultMaxWireBatchBytes = 4U * 1024U * 1024U;
constexpr std::size_t kDefaultMaxWireBatchEntries = 5000U;
constexpr std::size_t kDefaultMaxInflightWireBatches = 8U;
constexpr std::size_t kDefaultMaxInflightWireBytes = 16U * 1024U * 1024U;
constexpr std::size_t kDefaultMaxInflightWireBytesPerPeer = 8U * 1024U * 1024U;
constexpr std::size_t kDefaultMaxInflightWireBatchesPerPeer = 4U;
constexpr double kDefaultPeerRateLimitEntriesPerSecond = 2000.0;
constexpr double kDefaultPeerRateLimitBurstEntries = 4000.0;
constexpr double kIngressApplyDurationEwmaAlpha = 0.2;
constexpr double kIngressReplicationLatencyEwmaAlpha = 0.2;

struct IngressLimits {
    std::size_t max_wire_batch_bytes = kDefaultMaxWireBatchBytes;
    std::size_t max_wire_batch_entries = kDefaultMaxWireBatchEntries;
    std::size_t max_inflight_wire_batches = kDefaultMaxInflightWireBatches;
    std::size_t max_inflight_wire_bytes = kDefaultMaxInflightWireBytes;
    std::size_t max_inflight_wire_bytes_per_peer = kDefaultMaxInflightWireBytesPerPeer;
    std::size_t max_inflight_wire_batches_per_peer = kDefaultMaxInflightWireBatchesPerPeer;
    double peer_rate_limit_entries_per_second = kDefaultPeerRateLimitEntriesPerSecond;
    double peer_rate_limit_burst_entries = kDefaultPeerRateLimitBurstEntries;
};

std::optional<long long> parse_env_integer(const char* const name) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return std::nullopt;
    }
    char* end = nullptr;
    const long long parsed = std::strtoll(raw, &end, 10);
    if (end == raw || (end != nullptr && *end != '\0')) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<double> parse_env_double(const char* const name) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return std::nullopt;
    }
    char* end = nullptr;
    const double parsed = std::strtod(raw, &end);
    if (end == raw || (end != nullptr && *end != '\0')) {
        return std::nullopt;
    }
    return parsed;
}

std::size_t read_env_size_t(const char* const name, const std::size_t fallback, const std::size_t minimum) {
    const auto parsed = parse_env_integer(name);
    if (!parsed.has_value() || *parsed < static_cast<long long>(minimum)) {
        return fallback;
    }
    return static_cast<std::size_t>(*parsed);
}

double read_env_double(const char* const name, const double fallback, const double minimum) {
    const auto parsed = parse_env_double(name);
    if (!parsed.has_value() || *parsed < minimum) {
        return fallback;
    }
    return *parsed;
}

IngressLimits read_ingress_limits() {
    IngressLimits limits;
    limits.max_wire_batch_bytes = read_env_size_t(
        "TIGHTROPE_SYNC_MAX_WIRE_BATCH_BYTES",
        kDefaultMaxWireBatchBytes,
        1U);
    limits.max_wire_batch_entries = read_env_size_t(
        "TIGHTROPE_SYNC_MAX_WIRE_BATCH_ENTRIES",
        kDefaultMaxWireBatchEntries,
        1U);
    limits.max_inflight_wire_batches = read_env_size_t(
        "TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BATCHES",
        kDefaultMaxInflightWireBatches,
        1U);
    limits.max_inflight_wire_bytes = read_env_size_t(
        "TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BYTES",
        kDefaultMaxInflightWireBytes,
        1U);
    limits.max_inflight_wire_bytes_per_peer = read_env_size_t(
        "TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BYTES_PER_PEER",
        kDefaultMaxInflightWireBytesPerPeer,
        1U);
    limits.max_inflight_wire_batches_per_peer = read_env_size_t(
        "TIGHTROPE_SYNC_MAX_INFLIGHT_WIRE_BATCHES_PER_PEER",
        kDefaultMaxInflightWireBatchesPerPeer,
        1U);
    limits.peer_rate_limit_entries_per_second = read_env_double(
        "TIGHTROPE_SYNC_PEER_RATE_LIMIT_ENTRIES_PER_SECOND",
        kDefaultPeerRateLimitEntriesPerSecond,
        0.0);
    limits.peer_rate_limit_burst_entries = read_env_double(
        "TIGHTROPE_SYNC_PEER_RATE_LIMIT_BURST_ENTRIES",
        kDefaultPeerRateLimitBurstEntries,
        1.0);
    return limits;
}

std::atomic<std::size_t>& inflight_wire_batches() {
    static std::atomic<std::size_t> count{0};
    return count;
}

class InflightBatchGuard final {
public:
    explicit InflightBatchGuard(const std::size_t limit) : limit_(limit) {
        auto& inflight = inflight_wire_batches();
        std::size_t current = inflight.load(std::memory_order_relaxed);
        while (current < limit_) {
            if (inflight.compare_exchange_weak(current, current + 1U, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                acquired_ = true;
                return;
            }
        }
    }

    ~InflightBatchGuard() {
        if (!acquired_) {
            return;
        }
        inflight_wire_batches().fetch_sub(1U, std::memory_order_release);
    }

    [[nodiscard]] bool acquired() const noexcept {
        return acquired_;
    }

private:
    std::size_t limit_;
    bool acquired_ = false;
};

struct InflightWirePeerState {
    std::size_t batches = 0;
    std::size_t bytes = 0;
    std::size_t peak_batches = 0;
    std::size_t peak_bytes = 0;
};

struct InflightWirePeerSnapshot {
    std::uint64_t batches = 0;
    std::uint64_t peak_batches = 0;
    std::uint64_t bytes = 0;
    std::uint64_t peak_bytes = 0;
};

std::mutex& inflight_wire_budget_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::size_t& inflight_wire_total_bytes() {
    static std::size_t bytes = 0;
    return bytes;
}

std::unordered_map<std::uint32_t, InflightWirePeerState>& inflight_wire_peer_state() {
    static std::unordered_map<std::uint32_t, InflightWirePeerState> state;
    return state;
}

std::uint64_t saturating_u64_from_size_t(const std::size_t value) {
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        const auto max_u64_as_size_t = static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max());
        if (value > max_u64_as_size_t) {
            return std::numeric_limits<std::uint64_t>::max();
        }
    }
    return static_cast<std::uint64_t>(value);
}

std::optional<InflightWirePeerSnapshot> inflight_wire_peer_snapshot(const std::uint32_t peer_site_id) {
    if (peer_site_id == 0) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(inflight_wire_budget_mutex());
    const auto& by_peer = inflight_wire_peer_state();
    const auto it = by_peer.find(peer_site_id);
    if (it == by_peer.end()) {
        return std::nullopt;
    }
    return InflightWirePeerSnapshot{
        .batches = saturating_u64_from_size_t(it->second.batches),
        .peak_batches = saturating_u64_from_size_t(it->second.peak_batches),
        .bytes = saturating_u64_from_size_t(it->second.bytes),
        .peak_bytes = saturating_u64_from_size_t(it->second.peak_bytes),
    };
}

class InflightWireBudgetGuard final {
public:
    InflightWireBudgetGuard(
        const std::uint32_t peer_site_id,
        const std::size_t wire_bytes,
        const IngressLimits& limits,
        std::string* error
    )
        : peer_site_id_(peer_site_id), wire_bytes_(wire_bytes) {
        std::lock_guard<std::mutex> lock(inflight_wire_budget_mutex());
        auto& total_bytes = inflight_wire_total_bytes();
        auto& by_peer = inflight_wire_peer_state();
        auto& peer = by_peer[peer_site_id_];

        if (wire_bytes_ > limits.max_inflight_wire_bytes || total_bytes > (limits.max_inflight_wire_bytes - wire_bytes_)) {
            if (error != nullptr) {
                *error = "in-flight wire bytes budget exceeded (incoming=" + std::to_string(wire_bytes_) + ", max=" +
                         std::to_string(limits.max_inflight_wire_bytes) + ")";
            }
            return;
        }
        if (peer.batches >= limits.max_inflight_wire_batches_per_peer) {
            if (error != nullptr) {
                *error = "peer " + std::to_string(peer_site_id_) + " exceeds in-flight batch budget (max=" +
                         std::to_string(limits.max_inflight_wire_batches_per_peer) + ")";
            }
            return;
        }
        if (wire_bytes_ > limits.max_inflight_wire_bytes_per_peer ||
            peer.bytes > (limits.max_inflight_wire_bytes_per_peer - wire_bytes_)) {
            if (error != nullptr) {
                *error = "peer " + std::to_string(peer_site_id_) + " in-flight wire bytes budget exceeded (incoming=" +
                         std::to_string(wire_bytes_) + ", max=" + std::to_string(limits.max_inflight_wire_bytes_per_peer) + ")";
            }
            return;
        }

        total_bytes += wire_bytes_;
        ++peer.batches;
        peer.bytes += wire_bytes_;
        peer.peak_batches = std::max(peer.peak_batches, peer.batches);
        peer.peak_bytes = std::max(peer.peak_bytes, peer.bytes);
        acquired_ = true;
    }

    ~InflightWireBudgetGuard() {
        if (!acquired_) {
            return;
        }
        std::lock_guard<std::mutex> lock(inflight_wire_budget_mutex());
        auto& total_bytes = inflight_wire_total_bytes();
        if (total_bytes >= wire_bytes_) {
            total_bytes -= wire_bytes_;
        } else {
            total_bytes = 0;
        }

        auto& by_peer = inflight_wire_peer_state();
        auto it = by_peer.find(peer_site_id_);
        if (it == by_peer.end()) {
            return;
        }
        if (it->second.batches > 0) {
            --it->second.batches;
        }
        if (it->second.bytes >= wire_bytes_) {
            it->second.bytes -= wire_bytes_;
        } else {
            it->second.bytes = 0;
        }
    }

    [[nodiscard]] bool acquired() const noexcept {
        return acquired_;
    }

private:
    std::uint32_t peer_site_id_ = 0;
    std::size_t wire_bytes_ = 0;
    bool acquired_ = false;
};

struct PeerRateBucket {
    double tokens = 0.0;
    std::chrono::steady_clock::time_point last_refill{};
    bool initialized = false;
};

std::mutex& peer_rate_limit_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::uint32_t, PeerRateBucket>& peer_rate_limit_buckets() {
    static std::unordered_map<std::uint32_t, PeerRateBucket> buckets;
    return buckets;
}

core::time::Clock& runtime_clock() {
    static core::time::SystemClock clock;
    return clock;
}

std::uint64_t now_unix_ms() {
    const auto now = runtime_clock().unix_ms_now();
    return now > 0 ? static_cast<std::uint64_t>(now) : 0;
}

std::uint64_t elapsed_ms(const std::chrono::steady_clock::time_point started_at) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(runtime_clock().steady_now() - started_at);
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed.count()));
}

std::optional<std::uint64_t> batch_replication_latency_ms(
    const JournalBatchFrame& batch,
    const std::uint64_t observed_at_unix_ms
) {
    if (batch.entries.empty()) {
        return std::nullopt;
    }
    std::uint64_t max_latency_ms = 0;
    for (const auto& entry : batch.entries) {
        const auto entry_wall_ms = entry.hlc_wall;
        const auto latency_ms =
            observed_at_unix_ms > entry_wall_ms ? (observed_at_unix_ms - entry_wall_ms) : std::uint64_t{0};
        max_latency_ms = std::max(max_latency_ms, latency_ms);
    }
    return max_latency_ms;
}

std::mutex& peer_ingress_telemetry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::uint32_t, PeerIngressTelemetry>& peer_ingress_telemetry_map() {
    static std::unordered_map<std::uint32_t, PeerIngressTelemetry> telemetry;
    return telemetry;
}

std::uint64_t saturating_add_u64(const std::uint64_t left, const std::uint64_t right) {
    return right > (std::numeric_limits<std::uint64_t>::max() - left)
             ? std::numeric_limits<std::uint64_t>::max()
             : left + right;
}

std::string_view rejection_reason_label(const PeerIngressRejectionReason reason) {
    switch (reason) {
    case PeerIngressRejectionReason::BatchTooLarge:
        return "batch_too_large";
    case PeerIngressRejectionReason::Backpressure:
        return "backpressure";
    case PeerIngressRejectionReason::InflightWireBudget:
        return "inflight_wire_budget";
    case PeerIngressRejectionReason::HandshakeAuth:
        return "handshake_auth";
    case PeerIngressRejectionReason::HandshakeSchema:
        return "handshake_schema";
    case PeerIngressRejectionReason::InvalidWireBatch:
        return "invalid_wire_batch";
    case PeerIngressRejectionReason::EntryLimit:
        return "entry_limit";
    case PeerIngressRejectionReason::RateLimit:
        return "rate_limit";
    case PeerIngressRejectionReason::ApplyBatch:
        return "apply_batch";
    case PeerIngressRejectionReason::IngressProtocol:
        return "ingress_protocol";
    case PeerIngressRejectionReason::Unknown:
    default:
        return "unknown";
    }
}

void note_rejection_reason(PeerIngressTelemetry& telemetry, const PeerIngressRejectionReason reason) {
    switch (reason) {
    case PeerIngressRejectionReason::BatchTooLarge:
        ++telemetry.rejected_batch_too_large;
        break;
    case PeerIngressRejectionReason::Backpressure:
        ++telemetry.rejected_backpressure;
        break;
    case PeerIngressRejectionReason::InflightWireBudget:
        ++telemetry.rejected_inflight_wire_budget;
        break;
    case PeerIngressRejectionReason::HandshakeAuth:
        ++telemetry.rejected_handshake_auth;
        break;
    case PeerIngressRejectionReason::HandshakeSchema:
        ++telemetry.rejected_handshake_schema;
        break;
    case PeerIngressRejectionReason::InvalidWireBatch:
        ++telemetry.rejected_invalid_wire_batch;
        break;
    case PeerIngressRejectionReason::EntryLimit:
        ++telemetry.rejected_entry_limit;
        break;
    case PeerIngressRejectionReason::RateLimit:
        ++telemetry.rejected_rate_limit;
        break;
    case PeerIngressRejectionReason::ApplyBatch:
        ++telemetry.rejected_apply_batch;
        break;
    case PeerIngressRejectionReason::IngressProtocol:
        ++telemetry.rejected_ingress_protocol;
        break;
    case PeerIngressRejectionReason::Unknown:
    default:
        break;
    }
}

void record_peer_ingress_result(
    const std::uint32_t peer_site_id,
    const std::uint64_t last_recv_seq_from_peer,
    const std::uint64_t last_applied_up_to_seq,
    const bool success,
    const std::size_t wire_bytes,
    const PeerIngressRejectionReason reason = PeerIngressRejectionReason::Unknown,
    std::string_view error = {},
    const std::uint64_t apply_duration_ms = 0,
    const std::optional<std::uint64_t> replication_latency_ms = std::nullopt
) {
    if (peer_site_id == 0) {
        return;
    }

    const auto now_ms = now_unix_ms();
    std::lock_guard<std::mutex> lock(peer_ingress_telemetry_mutex());
    auto& telemetry = peer_ingress_telemetry_map()[peer_site_id];
    telemetry.site_id = peer_site_id;
    telemetry.last_seen_unix_ms = now_ms;
    telemetry.last_reported_seq_from_peer = std::max(telemetry.last_reported_seq_from_peer, last_recv_seq_from_peer);
    telemetry.last_applied_up_to_seq = std::max(telemetry.last_applied_up_to_seq, last_applied_up_to_seq);
    telemetry.last_wire_batch_bytes = static_cast<std::uint64_t>(wire_bytes);
    telemetry.total_apply_duration_ms = saturating_add_u64(telemetry.total_apply_duration_ms, apply_duration_ms);
    telemetry.last_apply_duration_ms = apply_duration_ms;
    telemetry.max_apply_duration_ms = std::max(telemetry.max_apply_duration_ms, apply_duration_ms);
    const std::optional<double> seed = telemetry.apply_duration_samples > 0
                                         ? std::optional<double>(telemetry.apply_duration_ewma_ms)
                                         : std::nullopt;
    core::time::Ewma<double> apply_duration_ewma{kIngressApplyDurationEwmaAlpha, seed};
    telemetry.apply_duration_ewma_ms = apply_duration_ewma.update(static_cast<double>(apply_duration_ms));
    telemetry.apply_duration_samples =
        telemetry.apply_duration_samples < std::numeric_limits<std::uint64_t>::max()
          ? telemetry.apply_duration_samples + 1
          : telemetry.apply_duration_samples;
    if (replication_latency_ms.has_value()) {
        telemetry.total_replication_latency_ms =
            saturating_add_u64(telemetry.total_replication_latency_ms, *replication_latency_ms);
        telemetry.last_replication_latency_ms = *replication_latency_ms;
        telemetry.max_replication_latency_ms =
            std::max(telemetry.max_replication_latency_ms, *replication_latency_ms);
        const std::optional<double> replication_seed = telemetry.replication_latency_samples > 0
                                                         ? std::optional<double>(telemetry.replication_latency_ewma_ms)
                                                         : std::nullopt;
        core::time::Ewma<double> replication_ewma{kIngressReplicationLatencyEwmaAlpha, replication_seed};
        telemetry.replication_latency_ewma_ms = replication_ewma.update(static_cast<double>(*replication_latency_ms));
        telemetry.replication_latency_samples =
            telemetry.replication_latency_samples < std::numeric_limits<std::uint64_t>::max()
              ? telemetry.replication_latency_samples + 1
              : telemetry.replication_latency_samples;
    }
    if (success) {
        ++telemetry.accepted_batches;
        telemetry.accepted_wire_bytes =
            saturating_add_u64(telemetry.accepted_wire_bytes, static_cast<std::uint64_t>(wire_bytes));
        telemetry.consecutive_failures = 0;
        return;
    }
    ++telemetry.rejected_batches;
    telemetry.rejected_wire_bytes =
        saturating_add_u64(telemetry.rejected_wire_bytes, static_cast<std::uint64_t>(wire_bytes));
    telemetry.last_rejection_at_unix_ms = now_ms;
    telemetry.last_rejection_reason = std::string(rejection_reason_label(reason));
    note_rejection_reason(telemetry, reason);
    if (!error.empty()) {
        telemetry.last_rejection_error = std::string(error);
    }
    ++telemetry.consecutive_failures;
}

bool consume_peer_rate_limit_tokens(
    const std::uint32_t peer_site_id,
    const std::size_t entry_count,
    const IngressLimits& limits,
    std::string* error
) {
    if (entry_count == 0 || limits.peer_rate_limit_entries_per_second <= 0.0) {
        return true;
    }

    const double requested = static_cast<double>(entry_count);
    if (requested > limits.peer_rate_limit_burst_entries) {
        if (error != nullptr) {
            *error = "peer " + std::to_string(peer_site_id) + " batch entries " + std::to_string(entry_count) +
                     " exceed rate-limit burst " + std::to_string(static_cast<std::uint64_t>(limits.peer_rate_limit_burst_entries));
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(peer_rate_limit_mutex());
    auto& buckets = peer_rate_limit_buckets();
    auto& bucket = buckets[peer_site_id];
    const auto now = std::chrono::steady_clock::now();

    if (!bucket.initialized) {
        bucket.initialized = true;
        bucket.tokens = limits.peer_rate_limit_burst_entries;
        bucket.last_refill = now;
    } else {
        const auto elapsed_seconds =
            std::chrono::duration_cast<std::chrono::duration<double>>(now - bucket.last_refill).count();
        if (elapsed_seconds > 0.0) {
            bucket.tokens = std::min(
                limits.peer_rate_limit_burst_entries,
                bucket.tokens + (elapsed_seconds * limits.peer_rate_limit_entries_per_second));
            bucket.last_refill = now;
        }
    }

    bucket.tokens = std::min(bucket.tokens, limits.peer_rate_limit_burst_entries);
    if (bucket.tokens + 1e-9 < requested) {
        if (error != nullptr) {
            *error = "peer " + std::to_string(peer_site_id) + " rate limit exceeded: requested=" +
                     std::to_string(entry_count) + " available=" + std::to_string(static_cast<std::uint64_t>(bucket.tokens));
        }
        return false;
    }

    bucket.tokens -= requested;
    return true;
}

std::string sqlite_error(sqlite3* db) {
    const char* text = sqlite3_errmsg(db);
    return text == nullptr ? std::string("sqlite error") : std::string(text);
}

bool exec_sql(sqlite3* db, const char* sql, std::string* error) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc == SQLITE_OK) {
        return true;
    }
    if (error != nullptr) {
        *error = err != nullptr ? std::string(err) : sqlite_error(db);
    }
    if (err != nullptr) {
        sqlite3_free(err);
    }
    return false;
}

std::string column_text(sqlite3_stmt* stmt, const int index) {
    const auto* text = sqlite3_column_text(stmt, index);
    return text == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(text));
}

std::string existing_checksum_for_seq(sqlite3* db, sqlite3_stmt* stmt, const std::uint64_t seq) {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(seq));
    const int step = sqlite3_step(stmt);
    if (step == SQLITE_ROW) {
        return column_text(stmt, 0);
    }
    return {};
}

} // namespace

bool SyncEngine::recompute_checksums(sqlite3* db) {
    if (db == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "sync_engine", "recompute_checksums_rejected_null_db");
        return false;
    }
    log_sync_event(SyncLogLevel::Debug, "sync_engine", "recompute_checksums_begin");

    sqlite3_stmt* select_stmt = nullptr;
    const char* select_sql = R"sql(
        SELECT seq, table_name, row_pk, op, old_values, new_values
        FROM _sync_journal
        ORDER BY seq ASC;
    )sql";
    if (sqlite3_prepare_v2(db, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK || select_stmt == nullptr) {
        if (select_stmt != nullptr) {
            sqlite3_finalize(select_stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "sync_engine",
            "recompute_checksums_prepare_select_failed",
            sqlite_error(db));
        return false;
    }

    std::vector<std::pair<std::uint64_t, std::string>> updates;
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        const std::uint64_t seq = static_cast<std::uint64_t>(sqlite3_column_int64(select_stmt, 0));
        const auto checksum = journal_checksum(
            column_text(select_stmt, 1),
            column_text(select_stmt, 2),
            column_text(select_stmt, 3),
            column_text(select_stmt, 4),
            column_text(select_stmt, 5)
        );
        updates.emplace_back(seq, checksum);
    }
    sqlite3_finalize(select_stmt);
    log_sync_event(
        SyncLogLevel::Trace,
        "sync_engine",
        "recompute_checksums_selected",
        "rows=" + std::to_string(updates.size()));

    sqlite3_stmt* update_stmt = nullptr;
    const char* update_sql = "UPDATE _sync_journal SET checksum = ?1 WHERE seq = ?2;";
    if (sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, nullptr) != SQLITE_OK || update_stmt == nullptr) {
        if (update_stmt != nullptr) {
            sqlite3_finalize(update_stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "sync_engine",
            "recompute_checksums_prepare_update_failed",
            sqlite_error(db));
        return false;
    }

    for (const auto& update : updates) {
        sqlite3_reset(update_stmt);
        sqlite3_clear_bindings(update_stmt);
        sqlite3_bind_text(update_stmt, 1, update.second.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(update_stmt, 2, static_cast<sqlite3_int64>(update.first));
        if (sqlite3_step(update_stmt) != SQLITE_DONE) {
            sqlite3_finalize(update_stmt);
            log_sync_event(
                SyncLogLevel::Error,
                "sync_engine",
                "recompute_checksums_update_failed",
                "seq=" + std::to_string(update.first) + " error=" + sqlite_error(db));
            return false;
        }
    }
    sqlite3_finalize(update_stmt);
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_engine",
        "recompute_checksums_complete",
        "rows=" + std::to_string(updates.size()));
    return true;
}

JournalBatchFrame SyncEngine::build_batch(sqlite3* db, const std::uint64_t after_seq, const std::size_t limit) {
    JournalBatchFrame frame;
    frame.from_seq = after_seq;
    frame.to_seq = after_seq;
    if (db == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "sync_engine", "build_batch_rejected_null_db");
        return frame;
    }
    log_sync_event(
        SyncLogLevel::Trace,
        "sync_engine",
        "build_batch_begin",
        "after_seq=" + std::to_string(after_seq) + " limit=" + std::to_string(limit));

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"sql(
        SELECT seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id
        FROM _sync_journal
        WHERE seq > ?1
        ORDER BY seq ASC
        LIMIT ?2;
    )sql";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "sync_engine",
            "build_batch_prepare_failed",
            sqlite_error(db));
        return frame;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(after_seq));
    sqlite3_bind_int(stmt, 2, static_cast<int>(limit));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        JournalWireEntry entry;
        entry.seq = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 0));
        entry.hlc_wall = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 1));
        entry.hlc_counter = static_cast<std::uint32_t>(sqlite3_column_int(stmt, 2));
        entry.site_id = static_cast<std::uint32_t>(sqlite3_column_int(stmt, 3));
        entry.table_name = column_text(stmt, 4);
        entry.row_pk = column_text(stmt, 5);
        entry.op = column_text(stmt, 6);
        entry.old_values = column_text(stmt, 7);
        entry.new_values = column_text(stmt, 8);
        entry.checksum = column_text(stmt, 9);
        entry.applied = sqlite3_column_int(stmt, 10);
        entry.batch_id = column_text(stmt, 11);

        frame.to_seq = std::max(frame.to_seq, entry.seq);
        frame.entries.push_back(std::move(entry));
    }

    sqlite3_finalize(stmt);
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_engine",
        "build_batch_complete",
        "from_seq=" + std::to_string(frame.from_seq) + " to_seq=" + std::to_string(frame.to_seq) + " entries=" +
            std::to_string(frame.entries.size()));
    return frame;
}

ApplyBatchResult SyncEngine::apply_batch(sqlite3* db, const JournalBatchFrame& batch, const int applied_value) {
    ApplyBatchResult result;
    if (db == nullptr) {
        result.error = "db is null";
        log_sync_event(SyncLogLevel::Error, "sync_engine", "apply_batch_rejected_null_db");
        return result;
    }
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_engine",
        "apply_batch_begin",
        "entries=" + std::to_string(batch.entries.size()) + " from_seq=" + std::to_string(batch.from_seq) +
            " to_seq=" + std::to_string(batch.to_seq) + " applied=" + std::to_string(applied_value));

    std::string sql_error;
    if (!exec_sql(db, "BEGIN IMMEDIATE;", &sql_error)) {
        result.error = sql_error;
        log_sync_event(SyncLogLevel::Error, "sync_engine", "apply_batch_begin_transaction_failed", sql_error);
        return result;
    }

    sqlite3_stmt* insert_stmt = nullptr;
    sqlite3_stmt* select_stmt = nullptr;
    const char* insert_sql = R"sql(
        INSERT OR IGNORE INTO _sync_journal (
          seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id
        ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12);
    )sql";
    const char* select_sql = "SELECT checksum FROM _sync_journal WHERE seq = ?1;";

    if (sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr) != SQLITE_OK || insert_stmt == nullptr ||
        sqlite3_prepare_v2(db, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK || select_stmt == nullptr) {
        if (insert_stmt != nullptr) {
            sqlite3_finalize(insert_stmt);
        }
        if (select_stmt != nullptr) {
            sqlite3_finalize(select_stmt);
        }
        exec_sql(db, "ROLLBACK;", nullptr);
        result.error = sqlite_error(db);
        log_sync_event(
            SyncLogLevel::Error,
            "sync_engine",
            "apply_batch_prepare_failed",
            result.error);
        return result;
    }

    for (const auto& entry : batch.entries) {
        log_sync_event(
            SyncLogLevel::Trace,
            "sync_engine",
            "apply_batch_entry_begin",
            "seq=" + std::to_string(entry.seq) + " table=" + entry.table_name + " op=" + entry.op);
        if (!is_table_replicated(entry.table_name)) {
            sqlite3_finalize(insert_stmt);
            sqlite3_finalize(select_stmt);
            exec_sql(db, "ROLLBACK;", nullptr);
            result.error = "table is not replicated: " + entry.table_name;
            log_sync_event(SyncLogLevel::Warning, "sync_engine", "apply_batch_rejected_table_not_replicated", result.error);
            return result;
        }

        const auto expected =
            journal_checksum(entry.table_name, entry.row_pk, entry.op, entry.old_values, entry.new_values);
        if (expected != entry.checksum) {
            sqlite3_finalize(insert_stmt);
            sqlite3_finalize(select_stmt);
            exec_sql(db, "ROLLBACK;", nullptr);
            result.error = "checksum mismatch for seq " + std::to_string(entry.seq);
            log_sync_event(SyncLogLevel::Warning, "sync_engine", "apply_batch_rejected_checksum_mismatch", result.error);
            return result;
        }

        sqlite3_reset(insert_stmt);
        sqlite3_clear_bindings(insert_stmt);
        const auto batch_id = entry.batch_id.empty() ? generate_batch_id() : entry.batch_id;
        sqlite3_bind_int64(insert_stmt, 1, static_cast<sqlite3_int64>(entry.seq));
        sqlite3_bind_int64(insert_stmt, 2, static_cast<sqlite3_int64>(entry.hlc_wall));
        sqlite3_bind_int64(insert_stmt, 3, static_cast<sqlite3_int64>(entry.hlc_counter));
        sqlite3_bind_int64(insert_stmt, 4, static_cast<sqlite3_int64>(entry.site_id));
        sqlite3_bind_text(insert_stmt, 5, entry.table_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 6, entry.row_pk.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 7, entry.op.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 8, entry.old_values.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 9, entry.new_values.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 10, entry.checksum.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insert_stmt, 11, applied_value);
        sqlite3_bind_text(insert_stmt, 12, batch_id.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
            sqlite3_finalize(insert_stmt);
            sqlite3_finalize(select_stmt);
            exec_sql(db, "ROLLBACK;", nullptr);
            result.error = sqlite_error(db);
            log_sync_event(
                SyncLogLevel::Error,
                "sync_engine",
                "apply_batch_insert_failed",
                "seq=" + std::to_string(entry.seq) + " error=" + result.error);
            return result;
        }

        if (sqlite3_changes(db) == 0) {
            const auto existing_checksum = existing_checksum_for_seq(db, select_stmt, entry.seq);
            if (existing_checksum != entry.checksum) {
                sqlite3_finalize(insert_stmt);
                sqlite3_finalize(select_stmt);
                exec_sql(db, "ROLLBACK;", nullptr);
                result.error = "existing checksum mismatch for seq " + std::to_string(entry.seq);
                log_sync_event(
                    SyncLogLevel::Warning,
                    "sync_engine",
                    "apply_batch_rejected_existing_checksum_mismatch",
                    result.error);
                return result;
            }
        } else {
            ++result.applied_count;
        }
        result.applied_up_to_seq = std::max(result.applied_up_to_seq, entry.seq);
    }

    sqlite3_finalize(insert_stmt);
    sqlite3_finalize(select_stmt);
    if (!exec_sql(db, "COMMIT;", &sql_error)) {
        exec_sql(db, "ROLLBACK;", nullptr);
        result.error = sql_error;
        log_sync_event(SyncLogLevel::Error, "sync_engine", "apply_batch_commit_failed", sql_error);
        return result;
    }

    result.success = true;
    log_sync_event(
        SyncLogLevel::Info,
        "sync_engine",
        "apply_batch_complete",
        "applied_count=" + std::to_string(result.applied_count) + " applied_up_to_seq=" +
            std::to_string(result.applied_up_to_seq));
    return result;
}

ApplyBatchResult
SyncEngine::apply_wire_batch(sqlite3* db, const ApplyWireBatchRequest& request, const std::vector<std::uint8_t>& wire_batch) {
    ApplyBatchResult result;
    const auto started_at = runtime_clock().steady_now();
    const auto remote_site_id = request.remote_handshake.site_id;
    const auto remote_last_recv_seq = request.remote_handshake.last_recv_seq_from_peer;
    std::optional<std::uint64_t> replication_latency_ms = std::nullopt;
    const auto record_outcome = [&](const std::uint64_t applied_up_to_seq,
                                    const bool success,
                                    const PeerIngressRejectionReason reason,
                                    const std::string_view error_message) {
        record_peer_ingress_result(
            remote_site_id,
            remote_last_recv_seq,
            applied_up_to_seq,
            success,
            wire_batch.size(),
            reason,
            error_message,
            elapsed_ms(started_at),
            replication_latency_ms);
    };
    if (db == nullptr) {
        result.error = "db is null";
        log_sync_event(SyncLogLevel::Error, "sync_engine", "apply_wire_batch_rejected_null_db");
        record_outcome(0, false, PeerIngressRejectionReason::Unknown, result.error);
        return result;
    }
    const auto ingress_limits = read_ingress_limits();
    if (wire_batch.size() > ingress_limits.max_wire_batch_bytes) {
        result.error = "wire batch size " + std::to_string(wire_batch.size()) + " exceeds max " +
                       std::to_string(ingress_limits.max_wire_batch_bytes);
        log_sync_event(SyncLogLevel::Warning, "sync_engine", "apply_wire_batch_rejected_batch_too_large", result.error);
        record_outcome(0, false, PeerIngressRejectionReason::BatchTooLarge, result.error);
        return result;
    }

    InflightBatchGuard inflight_guard(ingress_limits.max_inflight_wire_batches);
    if (!inflight_guard.acquired()) {
        result.error = "wire batch backpressure: too many in-flight batches (max=" +
                       std::to_string(ingress_limits.max_inflight_wire_batches) + ")";
        log_sync_event(SyncLogLevel::Warning, "sync_engine", "apply_wire_batch_rejected_backpressure", result.error);
        record_outcome(0, false, PeerIngressRejectionReason::Backpressure, result.error);
        return result;
    }

    std::string inflight_budget_error;
    InflightWireBudgetGuard inflight_budget_guard(
        remote_site_id,
        wire_batch.size(),
        ingress_limits,
        &inflight_budget_error);
    if (!inflight_budget_guard.acquired()) {
        result.error = "wire batch backpressure: " + inflight_budget_error;
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_engine",
            "apply_wire_batch_rejected_inflight_wire_budget",
            result.error);
        record_outcome(0, false, PeerIngressRejectionReason::InflightWireBudget, result.error);
        return result;
    }

    const auto auth_validation = validate_handshake_auth(
        request.remote_handshake,
        request.cluster_shared_secret,
        request.require_handshake_auth);
    if (!auth_validation.accepted) {
        result.error = "handshake rejected: " + auth_validation.error;
        log_sync_event(SyncLogLevel::Warning, "sync_engine", "apply_wire_batch_rejected_handshake_auth", result.error);
        record_outcome(0, false, PeerIngressRejectionReason::HandshakeAuth, result.error);
        return result;
    }

    const auto validation = validate_handshake_schema_version(
        request.remote_handshake,
        request.local_schema_version,
        request.allow_schema_downgrade,
        request.min_supported_schema_version);
    if (!validation.accepted) {
        result.error = "handshake rejected: " + validation.error;
        log_sync_event(SyncLogLevel::Warning, "sync_engine", "apply_wire_batch_rejected_handshake", result.error);
        record_outcome(0, false, PeerIngressRejectionReason::HandshakeSchema, result.error);
        return result;
    }

    const auto decoded = decode_journal_batch(wire_batch);
    if (!decoded.has_value()) {
        result.error = "invalid wire journal batch";
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_engine",
            "apply_wire_batch_rejected_invalid_wire_batch",
            "bytes=" + std::to_string(wire_batch.size()));
        record_outcome(0, false, PeerIngressRejectionReason::InvalidWireBatch, result.error);
        return result;
    }
    replication_latency_ms = batch_replication_latency_ms(*decoded, now_unix_ms());
    if (decoded->entries.size() > ingress_limits.max_wire_batch_entries) {
        result.error = "wire batch entries " + std::to_string(decoded->entries.size()) + " exceed max " +
                       std::to_string(ingress_limits.max_wire_batch_entries);
        log_sync_event(SyncLogLevel::Warning, "sync_engine", "apply_wire_batch_rejected_too_many_entries", result.error);
        record_outcome(0, false, PeerIngressRejectionReason::EntryLimit, result.error);
        return result;
    }

    std::string rate_limit_error;
    if (!consume_peer_rate_limit_tokens(
            request.remote_handshake.site_id,
            decoded->entries.size(),
            ingress_limits,
            &rate_limit_error)) {
        result.error = "wire batch rejected: " + rate_limit_error;
        log_sync_event(SyncLogLevel::Warning, "sync_engine", "apply_wire_batch_rejected_rate_limit", result.error);
        record_outcome(0, false, PeerIngressRejectionReason::RateLimit, result.error);
        return result;
    }

    log_sync_event(
        SyncLogLevel::Debug,
        "sync_engine",
        "apply_wire_batch_begin",
        "remote_site_id=" + std::to_string(request.remote_handshake.site_id) + " local_schema=" +
            std::to_string(request.local_schema_version) + " negotiated_schema=" +
            std::to_string(validation.negotiated_schema_version) + " entries=" + std::to_string(decoded->entries.size()));
    const auto applied = apply_batch(db, *decoded, request.applied_value);
    record_outcome(
        applied.applied_up_to_seq,
        applied.success,
        applied.success ? PeerIngressRejectionReason::Unknown : PeerIngressRejectionReason::ApplyBatch,
        applied.error);
    return applied;
}

std::optional<PeerIngressTelemetry> SyncEngine::peer_ingress_telemetry(const std::uint32_t peer_site_id) {
    if (peer_site_id == 0) {
        return std::nullopt;
    }
    std::optional<PeerIngressTelemetry> snapshot = std::nullopt;
    {
        std::lock_guard<std::mutex> lock(peer_ingress_telemetry_mutex());
        const auto& telemetry = peer_ingress_telemetry_map();
        const auto it = telemetry.find(peer_site_id);
        if (it != telemetry.end()) {
            snapshot = it->second;
        }
    }
    if (const auto inflight = inflight_wire_peer_snapshot(peer_site_id); inflight.has_value()) {
        if (!snapshot.has_value()) {
            snapshot = PeerIngressTelemetry{};
            snapshot->site_id = peer_site_id;
        }
        snapshot->inflight_wire_batches = inflight->batches;
        snapshot->inflight_wire_batches_peak = inflight->peak_batches;
        snapshot->inflight_wire_bytes = inflight->bytes;
        snapshot->inflight_wire_bytes_peak = inflight->peak_bytes;
    }
    return snapshot;
}

void SyncEngine::record_peer_ingress_rejection(
    const std::uint32_t peer_site_id,
    const std::size_t wire_bytes,
    const PeerIngressRejectionReason reason,
    const std::string_view error
) {
    record_peer_ingress_result(
        peer_site_id,
        0,
        0,
        false,
        wire_bytes,
        reason,
        error);
}

void SyncEngine::reset_peer_ingress_telemetry_for_testing() {
    {
        std::lock_guard<std::mutex> lock(peer_ingress_telemetry_mutex());
        peer_ingress_telemetry_map().clear();
    }
    {
        std::lock_guard<std::mutex> lock(peer_rate_limit_mutex());
        peer_rate_limit_buckets().clear();
    }
    {
        std::lock_guard<std::mutex> lock(inflight_wire_budget_mutex());
        inflight_wire_total_bytes() = 0;
        inflight_wire_peer_state().clear();
    }
    inflight_wire_batches().store(0U, std::memory_order_release);
}

} // namespace tightrope::sync
