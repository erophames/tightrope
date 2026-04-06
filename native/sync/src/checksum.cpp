#include "checksum.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include <sodium.h>

#include "text/hex_codec.h"

namespace tightrope::sync {

namespace {

std::string journal_payload(
    const std::string_view table_name,
    const std::string_view row_pk,
    const std::string_view op,
    const std::string_view old_values,
    const std::string_view new_values
) {
    std::string payload;
    payload.reserve(table_name.size() + row_pk.size() + op.size() + old_values.size() + new_values.size() + 8);
    payload.append(table_name);
    payload.push_back('\x1F');
    payload.append(row_pk);
    payload.push_back('\x1F');
    payload.append(op);
    payload.push_back('\x1F');
    payload.append(old_values);
    payload.push_back('\x1F');
    payload.append(new_values);
    return payload;
}

} // namespace

std::string sha256_hex(const std::string_view input) {
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256(
        digest.data(),
        reinterpret_cast<const unsigned char*>(input.data()),
        static_cast<unsigned long long>(input.size())
    );
    return core::text::hex_encode(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(digest.data()), digest.size())
    );
}

std::string journal_checksum(
    const std::string_view table_name,
    const std::string_view row_pk,
    const std::string_view op,
    const std::string_view old_values,
    const std::string_view new_values
) {
    return sha256_hex(journal_payload(table_name, row_pk, op, old_values, new_values));
}

} // namespace tightrope::sync
