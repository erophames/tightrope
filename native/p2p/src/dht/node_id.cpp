#include "p2p/dht/node_id.h"

#include <algorithm>
#include <array>
#include <span>

#include <sodium.h>

#include "text/hex_codec.h"

namespace tightrope::p2p::dht {

namespace {

bool sodium_ready() {
    static const bool ready = []() {
        return sodium_init() >= 0;
    }();
    return ready;
}

} // namespace

NodeId NodeId::random() {
    NodeId id;
    if (sodium_ready()) {
        randombytes_buf(id.bytes_.data(), id.bytes_.size());
    }
    return id;
}

NodeId NodeId::hash_of(const std::string_view input) {
    NodeId id;
    if (!sodium_ready()) {
        return id;
    }
    crypto_hash_sha256(
        id.bytes_.data(),
        reinterpret_cast<const unsigned char*>(input.data()),
        static_cast<unsigned long long>(input.size())
    );
    return id;
}

std::optional<NodeId> NodeId::from_hex(const std::string_view hex) {
    std::array<std::uint8_t, kBytes> bytes{};
    if (!core::text::hex_decode(hex, std::span<std::uint8_t>(bytes.data(), bytes.size()))) {
        return std::nullopt;
    }
    return NodeId{bytes};
}

std::optional<NodeId> NodeId::from_bytes(const std::string_view bytes) {
    if (bytes.size() != kBytes) {
        return std::nullopt;
    }
    std::array<std::uint8_t, kBytes> out{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes.data()), kBytes, out.data());
    return NodeId{out};
}

std::string NodeId::to_hex() const {
    return core::text::hex_encode(std::span<const std::uint8_t>(bytes_.data(), bytes_.size()));
}

bool NodeId::is_zero() const noexcept {
    return std::all_of(bytes_.begin(), bytes_.end(), [](const std::uint8_t b) { return b == 0; });
}

bool operator<(const NodeId& lhs, const NodeId& rhs) noexcept {
    return std::lexicographical_compare(
        lhs.bytes_.begin(),
        lhs.bytes_.end(),
        rhs.bytes_.begin(),
        rhs.bytes_.end()
    );
}

NodeDistance xor_distance(const NodeId& lhs, const NodeId& rhs) noexcept {
    NodeDistance distance{};
    for (std::size_t i = 0; i < NodeId::kBytes; ++i) {
        distance.bytes[i] = static_cast<std::uint8_t>(lhs.bytes()[i] ^ rhs.bytes()[i]);
    }
    return distance;
}

bool distance_less(const NodeDistance& lhs, const NodeDistance& rhs) noexcept {
    return std::lexicographical_compare(
        lhs.bytes.begin(),
        lhs.bytes.end(),
        rhs.bytes.begin(),
        rhs.bytes.end()
    );
}

std::optional<std::size_t> bucket_index(const NodeId& local, const NodeId& remote) noexcept {
    const auto distance = xor_distance(local, remote);
    for (std::size_t byte_index = 0; byte_index < distance.bytes.size(); ++byte_index) {
        const std::uint8_t value = distance.bytes[byte_index];
        if (value == 0) {
            continue;
        }
        for (int bit = 7; bit >= 0; --bit) {
            if ((value & (1U << bit)) == 0U) {
                continue;
            }
            const auto absolute_bit = static_cast<std::size_t>((NodeId::kBytes - byte_index - 1) * 8 + bit);
            return absolute_bit;
        }
    }
    return std::nullopt;
}

} // namespace tightrope::p2p::dht
