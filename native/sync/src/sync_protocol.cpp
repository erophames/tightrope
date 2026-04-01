#include "sync_protocol.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <lz4.h>
#include <sodium.h>

#include "sync_logging.h"

namespace tightrope::sync {

namespace {

constexpr std::uint8_t kCompressedFlag = 0x1;

void write_u8(std::vector<std::uint8_t>& out, const std::uint8_t value) {
    out.push_back(value);
}

void write_u32(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void write_u64(std::vector<std::uint8_t>& out, const std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
}

void write_i32(std::vector<std::uint8_t>& out, const std::int32_t value) {
    write_u32(out, static_cast<std::uint32_t>(value));
}

bool write_string(std::vector<std::uint8_t>& out, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    write_u32(out, static_cast<std::uint32_t>(value.size()));
    const auto offset = out.size();
    out.resize(offset + value.size());
    boost::asio::buffer_copy(
        boost::asio::buffer(out.data() + offset, value.size()),
        boost::asio::buffer(value.data(), value.size())
    );
    return true;
}

bool read_u8(std::size_t& cursor, const std::vector<std::uint8_t>& in, std::uint8_t& out) {
    if (cursor + 1 > in.size()) {
        return false;
    }
    out = in[cursor];
    ++cursor;
    return true;
}

bool read_u32(std::size_t& cursor, const std::vector<std::uint8_t>& in, std::uint32_t& out) {
    if (cursor + 4 > in.size()) {
        return false;
    }
    out = static_cast<std::uint32_t>(in[cursor]) | (static_cast<std::uint32_t>(in[cursor + 1]) << 8U) |
          (static_cast<std::uint32_t>(in[cursor + 2]) << 16U) | (static_cast<std::uint32_t>(in[cursor + 3]) << 24U);
    cursor += 4;
    return true;
}

bool read_u64(std::size_t& cursor, const std::vector<std::uint8_t>& in, std::uint64_t& out) {
    if (cursor + 8 > in.size()) {
        return false;
    }
    out = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        out |= (static_cast<std::uint64_t>(in[cursor++]) << shift);
    }
    return true;
}

bool read_i32(std::size_t& cursor, const std::vector<std::uint8_t>& in, std::int32_t& out) {
    std::uint32_t raw = 0;
    if (!read_u32(cursor, in, raw)) {
        return false;
    }
    out = static_cast<std::int32_t>(raw);
    return true;
}

bool read_string(std::size_t& cursor, const std::vector<std::uint8_t>& in, std::string& out) {
    std::uint32_t size = 0;
    if (!read_u32(cursor, in, size)) {
        return false;
    }
    if (cursor + size > in.size()) {
        return false;
    }
    out.resize(size);
    boost::asio::buffer_copy(
        boost::asio::buffer(out.data(), size),
        boost::asio::buffer(in.data() + cursor, size)
    );
    cursor += size;
    return true;
}

std::string to_hex(const unsigned char* bytes, const std::size_t size) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.resize(size * 2);
    for (std::size_t index = 0; index < size; ++index) {
        const auto value = bytes[index];
        hex[index * 2] = kHex[(value >> 4U) & 0x0F];
        hex[index * 2 + 1] = kHex[value & 0x0F];
    }
    return hex;
}

std::optional<unsigned char> decode_hex_nibble(const char ch) {
    if (ch >= '0' && ch <= '9') {
        return static_cast<unsigned char>(ch - '0');
    }
    const auto lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (lowered >= 'a' && lowered <= 'f') {
        return static_cast<unsigned char>(10 + (lowered - 'a'));
    }
    return std::nullopt;
}

bool decode_hmac_hex(
    const std::string_view hex,
    std::array<unsigned char, crypto_auth_hmacsha256_BYTES>& out
) {
    if (hex.size() != out.size() * 2) {
        return false;
    }
    for (std::size_t index = 0; index < out.size(); ++index) {
        const auto high = decode_hex_nibble(hex[index * 2]);
        const auto low = decode_hex_nibble(hex[index * 2 + 1]);
        if (!high.has_value() || !low.has_value()) {
            return false;
        }
        out[index] = static_cast<unsigned char>(((*high) << 4U) | *low);
    }
    return true;
}

std::vector<std::uint8_t> handshake_auth_payload(const HandshakeFrame& frame) {
    std::vector<std::uint8_t> payload;
    payload.reserve(16 + 4 + frame.auth_key_id.size());
    write_u32(payload, frame.site_id);
    write_u32(payload, frame.schema_version);
    write_u64(payload, frame.last_recv_seq_from_peer);
    write_string(payload, frame.auth_key_id);
    return payload;
}

std::optional<std::string> compute_handshake_hmac_hex(const HandshakeFrame& frame, const std::string_view shared_secret) {
    if (shared_secret.empty()) {
        return std::nullopt;
    }
    std::array<unsigned char, crypto_auth_hmacsha256_BYTES> digest{};
    const auto payload = handshake_auth_payload(frame);
    crypto_auth_hmacsha256_state state{};
    crypto_auth_hmacsha256_init(
        &state,
        reinterpret_cast<const unsigned char*>(shared_secret.data()),
        shared_secret.size()
    );
    crypto_auth_hmacsha256_update(
        &state,
        reinterpret_cast<const unsigned char*>(payload.data()),
        static_cast<unsigned long long>(payload.size())
    );
    crypto_auth_hmacsha256_final(&state, digest.data());
    return to_hex(digest.data(), digest.size());
}

std::optional<std::vector<std::uint8_t>> compress_lz4(const std::vector<std::uint8_t>& payload) {
    const auto max_size = LZ4_compressBound(static_cast<int>(payload.size()));
    if (max_size <= 0) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> out(static_cast<std::size_t>(max_size));
    const auto written = LZ4_compress_default(
        reinterpret_cast<const char*>(payload.data()),
        reinterpret_cast<char*>(out.data()),
        static_cast<int>(payload.size()),
        max_size
    );
    if (written <= 0) {
        return std::nullopt;
    }
    out.resize(static_cast<std::size_t>(written));
    return out;
}

std::optional<std::vector<std::uint8_t>>
decompress_lz4(const std::vector<std::uint8_t>& payload, const std::size_t uncompressed_size) {
    std::vector<std::uint8_t> out(uncompressed_size);
    const auto written = LZ4_decompress_safe(
        reinterpret_cast<const char*>(payload.data()),
        reinterpret_cast<char*>(out.data()),
        static_cast<int>(payload.size()),
        static_cast<int>(uncompressed_size)
    );
    if (written < 0 || static_cast<std::size_t>(written) != uncompressed_size) {
        return std::nullopt;
    }
    return out;
}

} // namespace

std::vector<std::uint8_t> encode_handshake(const HandshakeFrame& frame) {
    std::vector<std::uint8_t> out;
    out.reserve(16 + (frame.auth_key_id.empty() ? 0 : frame.auth_key_id.size() + 4) +
                (frame.auth_hmac_hex.empty() ? 0 : frame.auth_hmac_hex.size() + 4));
    write_u32(out, frame.site_id);
    write_u32(out, frame.schema_version);
    write_u64(out, frame.last_recv_seq_from_peer);
    if (!frame.auth_key_id.empty() || !frame.auth_hmac_hex.empty()) {
        if (!write_string(out, frame.auth_key_id) || !write_string(out, frame.auth_hmac_hex)) {
            log_sync_event(
                SyncLogLevel::Error,
                "sync_protocol",
                "encode_handshake_failed",
                "site_id=" + std::to_string(frame.site_id) + " reason=auth_field_too_large");
            return {};
        }
    }
    log_sync_event(
        SyncLogLevel::Trace,
        "sync_protocol",
        "encode_handshake",
        "site_id=" + std::to_string(frame.site_id) + " schema=" + std::to_string(frame.schema_version) +
            " last_recv=" + std::to_string(frame.last_recv_seq_from_peer) +
            " auth=" + (frame.auth_hmac_hex.empty() ? std::string("0") : std::string("1")) +
            " bytes=" + std::to_string(out.size()));
    return out;
}

std::optional<HandshakeFrame> decode_handshake(const std::vector<std::uint8_t>& bytes) {
    std::size_t cursor = 0;
    HandshakeFrame frame;
    if (!read_u32(cursor, bytes, frame.site_id) || !read_u32(cursor, bytes, frame.schema_version) ||
        !read_u64(cursor, bytes, frame.last_recv_seq_from_peer)) {
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "decode_handshake_failed",
            "bytes=" + std::to_string(bytes.size()));
        return std::nullopt;
    }
    if (cursor != bytes.size()) {
        if (!read_string(cursor, bytes, frame.auth_key_id) || !read_string(cursor, bytes, frame.auth_hmac_hex) ||
            cursor != bytes.size()) {
            log_sync_event(
                SyncLogLevel::Warning,
                "sync_protocol",
                "decode_handshake_failed",
                "bytes=" + std::to_string(bytes.size()) + " reason=invalid_auth_fields");
            return std::nullopt;
        }
    }
    log_sync_event(
        SyncLogLevel::Trace,
        "sync_protocol",
        "decode_handshake",
        "site_id=" + std::to_string(frame.site_id) + " schema=" + std::to_string(frame.schema_version) +
            " last_recv=" + std::to_string(frame.last_recv_seq_from_peer) +
            " auth=" + (frame.auth_hmac_hex.empty() ? std::string("0") : std::string("1")));
    return frame;
}

void sign_handshake(HandshakeFrame& frame, const std::string_view shared_secret) {
    if (const auto signature = compute_handshake_hmac_hex(frame, shared_secret); signature.has_value()) {
        frame.auth_hmac_hex = *signature;
        return;
    }
    frame.auth_hmac_hex.clear();
}

HandshakeAuthValidationResult validate_handshake_auth(
    const HandshakeFrame& remote,
    const std::string_view shared_secret,
    const bool require_auth
) {
    HandshakeAuthValidationResult result{};
    if (remote.auth_hmac_hex.empty()) {
        if (require_auth) {
            result.error = "handshake auth required but missing hmac";
            log_sync_event(
                SyncLogLevel::Warning,
                "sync_protocol",
                "validate_handshake_auth_rejected",
                "site_id=" + std::to_string(remote.site_id) + " error=" + result.error);
            return result;
        }
        result.accepted = true;
        return result;
    }

    if (shared_secret.empty()) {
        result.error = "handshake auth present but shared secret is not configured";
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "validate_handshake_auth_rejected",
            "site_id=" + std::to_string(remote.site_id) + " error=" + result.error);
        return result;
    }

    std::array<unsigned char, crypto_auth_hmacsha256_BYTES> provided{};
    if (!decode_hmac_hex(remote.auth_hmac_hex, provided)) {
        result.error = "handshake auth hmac must be 64 hex characters";
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "validate_handshake_auth_rejected",
            "site_id=" + std::to_string(remote.site_id) + " error=" + result.error);
        return result;
    }

    const auto expected_hex = compute_handshake_hmac_hex(remote, shared_secret);
    if (!expected_hex.has_value()) {
        result.error = "handshake auth could not be computed";
        log_sync_event(
            SyncLogLevel::Error,
            "sync_protocol",
            "validate_handshake_auth_rejected",
            "site_id=" + std::to_string(remote.site_id) + " error=" + result.error);
        return result;
    }

    std::array<unsigned char, crypto_auth_hmacsha256_BYTES> expected{};
    if (!decode_hmac_hex(*expected_hex, expected)) {
        result.error = "handshake auth expected digest is invalid";
        log_sync_event(
            SyncLogLevel::Error,
            "sync_protocol",
            "validate_handshake_auth_rejected",
            "site_id=" + std::to_string(remote.site_id) + " error=" + result.error);
        return result;
    }

    if (crypto_verify_32(provided.data(), expected.data()) != 0) {
        result.error = "handshake auth hmac mismatch";
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "validate_handshake_auth_rejected",
            "site_id=" + std::to_string(remote.site_id) + " error=" + result.error);
        return result;
    }

    result.accepted = true;
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_protocol",
        "validate_handshake_auth_accepted",
        "site_id=" + std::to_string(remote.site_id));
    return result;
}

HandshakeValidationResult validate_handshake_schema_version(
    const HandshakeFrame& remote,
    const std::uint32_t local_schema_version,
    const bool allow_downgrade,
    const std::uint32_t min_supported_schema_version
) {
    HandshakeValidationResult result{};
    if (min_supported_schema_version == 0) {
        result.error = "min_supported_schema_version must be >= 1";
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "validate_handshake_schema_rejected",
            result.error);
        return result;
    }

    if (local_schema_version < min_supported_schema_version) {
        result.error = "local schema version " + std::to_string(local_schema_version) +
                       " is below minimum supported " + std::to_string(min_supported_schema_version);
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "validate_handshake_schema_rejected",
            result.error);
        return result;
    }

    if (remote.schema_version < min_supported_schema_version) {
        result.error = "peer schema version " + std::to_string(remote.schema_version) +
                       " is below minimum supported " + std::to_string(min_supported_schema_version);
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "validate_handshake_schema_rejected",
            result.error);
        return result;
    }

    if (remote.schema_version == local_schema_version) {
        result.accepted = true;
        result.negotiated_schema_version = local_schema_version;
        log_sync_event(
            SyncLogLevel::Debug,
            "sync_protocol",
            "validate_handshake_schema_accepted",
            "mode=strict negotiated_schema=" + std::to_string(result.negotiated_schema_version));
        return result;
    }

    if (!allow_downgrade) {
        result.error = "schema version mismatch: local=" + std::to_string(local_schema_version) +
                       " peer=" + std::to_string(remote.schema_version);
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "validate_handshake_schema_rejected",
            result.error);
        return result;
    }

    const auto negotiated = std::min(local_schema_version, remote.schema_version);
    if (negotiated < min_supported_schema_version) {
        result.error = "no compatible schema version: local=" + std::to_string(local_schema_version) +
                       " peer=" + std::to_string(remote.schema_version) +
                       " min_supported=" + std::to_string(min_supported_schema_version);
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "validate_handshake_schema_rejected",
            result.error);
        return result;
    }

    result.accepted = true;
    result.negotiated_schema_version = negotiated;
    log_sync_event(
        SyncLogLevel::Info,
        "sync_protocol",
        "validate_handshake_schema_accepted",
        "mode=downgrade negotiated_schema=" + std::to_string(result.negotiated_schema_version) + " local=" +
            std::to_string(local_schema_version) + " peer=" + std::to_string(remote.schema_version));
    return result;
}

std::vector<std::uint8_t> encode_journal_batch(const JournalBatchFrame& frame) {
    std::vector<std::uint8_t> payload;
    payload.reserve(64 + frame.entries.size() * 256);
    write_u64(payload, frame.from_seq);
    write_u64(payload, frame.to_seq);
    write_u32(payload, static_cast<std::uint32_t>(frame.entries.size()));

    for (const auto& entry : frame.entries) {
        write_u64(payload, entry.seq);
        write_u64(payload, entry.hlc_wall);
        write_u32(payload, entry.hlc_counter);
        write_u32(payload, entry.site_id);
        write_string(payload, entry.table_name);
        write_string(payload, entry.row_pk);
        write_string(payload, entry.op);
        write_string(payload, entry.old_values);
        write_string(payload, entry.new_values);
        write_string(payload, entry.checksum);
        write_i32(payload, entry.applied);
        write_string(payload, entry.batch_id);
    }

    const auto compressed = compress_lz4(payload);
    const bool use_compressed = compressed.has_value() && compressed->size() < payload.size();
    const auto& wire_payload = use_compressed ? *compressed : payload;

    std::vector<std::uint8_t> wire;
    wire.reserve(1 + 4 + 4 + wire_payload.size());
    write_u8(wire, use_compressed ? kCompressedFlag : 0);
    write_u32(wire, static_cast<std::uint32_t>(payload.size()));
    write_u32(wire, static_cast<std::uint32_t>(wire_payload.size()));
    wire.insert(wire.end(), wire_payload.begin(), wire_payload.end());
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_protocol",
        "encode_journal_batch",
        "entries=" + std::to_string(frame.entries.size()) + " from_seq=" + std::to_string(frame.from_seq) +
            " to_seq=" + std::to_string(frame.to_seq) + " payload_bytes=" + std::to_string(payload.size()) +
            " wire_bytes=" + std::to_string(wire.size()) + " compressed=" + std::string(use_compressed ? "1" : "0"));
    return wire;
}

std::optional<JournalBatchFrame> decode_journal_batch(const std::vector<std::uint8_t>& bytes) {
    std::size_t cursor = 0;
    std::uint8_t flags = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint32_t payload_size = 0;

    if (!read_u8(cursor, bytes, flags) || !read_u32(cursor, bytes, uncompressed_size) ||
        !read_u32(cursor, bytes, payload_size) || cursor + payload_size != bytes.size()) {
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "decode_journal_batch_invalid_header",
            "bytes=" + std::to_string(bytes.size()));
        return std::nullopt;
    }

    std::vector<std::uint8_t> payload(payload_size);
    boost::asio::buffer_copy(
        boost::asio::buffer(payload.data(), payload_size),
        boost::asio::buffer(bytes.data() + cursor, payload_size)
    );

    if ((flags & kCompressedFlag) != 0) {
        auto decompressed = decompress_lz4(payload, uncompressed_size);
        if (!decompressed.has_value()) {
            log_sync_event(
                SyncLogLevel::Warning,
                "sync_protocol",
                "decode_journal_batch_decompress_failed",
                "payload_bytes=" + std::to_string(payload.size()) + " uncompressed_bytes=" +
                    std::to_string(uncompressed_size));
            return std::nullopt;
        }
        payload = std::move(*decompressed);
    }

    std::size_t parse_cursor = 0;
    JournalBatchFrame batch;
    std::uint32_t count = 0;
    if (!read_u64(parse_cursor, payload, batch.from_seq) || !read_u64(parse_cursor, payload, batch.to_seq) ||
        !read_u32(parse_cursor, payload, count)) {
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "decode_journal_batch_invalid_prefix",
            "payload_bytes=" + std::to_string(payload.size()));
        return std::nullopt;
    }

    batch.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        JournalWireEntry entry;
        if (!read_u64(parse_cursor, payload, entry.seq) || !read_u64(parse_cursor, payload, entry.hlc_wall) ||
            !read_u32(parse_cursor, payload, entry.hlc_counter) || !read_u32(parse_cursor, payload, entry.site_id) ||
            !read_string(parse_cursor, payload, entry.table_name) || !read_string(parse_cursor, payload, entry.row_pk) ||
            !read_string(parse_cursor, payload, entry.op) || !read_string(parse_cursor, payload, entry.old_values) ||
            !read_string(parse_cursor, payload, entry.new_values) || !read_string(parse_cursor, payload, entry.checksum) ||
            !read_i32(parse_cursor, payload, entry.applied) || !read_string(parse_cursor, payload, entry.batch_id)) {
            log_sync_event(
                SyncLogLevel::Warning,
                "sync_protocol",
                "decode_journal_batch_invalid_entry",
                "index=" + std::to_string(i));
            return std::nullopt;
        }
        batch.entries.push_back(std::move(entry));
    }

    if (parse_cursor != payload.size()) {
        log_sync_event(
            SyncLogLevel::Warning,
            "sync_protocol",
            "decode_journal_batch_trailing_bytes",
            "remaining=" + std::to_string(payload.size() - parse_cursor));
        return std::nullopt;
    }
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_protocol",
        "decode_journal_batch",
        "entries=" + std::to_string(batch.entries.size()) + " from_seq=" + std::to_string(batch.from_seq) +
            " to_seq=" + std::to_string(batch.to_seq));
    return batch;
}

} // namespace tightrope::sync
