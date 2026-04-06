#pragma once
// Stable DHT node identifier helpers (SHA-256 based, 256-bit keyspace).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tightrope::p2p::dht {

class NodeId {
public:
    static constexpr std::size_t kBytes = 32;

    NodeId() = default;
    explicit NodeId(const std::array<std::uint8_t, kBytes>& bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] static NodeId random();
    [[nodiscard]] static NodeId hash_of(std::string_view input);
    [[nodiscard]] static std::optional<NodeId> from_hex(std::string_view hex);
    [[nodiscard]] static std::optional<NodeId> from_bytes(std::string_view bytes);

    [[nodiscard]] const std::array<std::uint8_t, kBytes>& bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::string to_hex() const;
    [[nodiscard]] bool is_zero() const noexcept;

    friend bool operator==(const NodeId&, const NodeId&) = default;
    friend bool operator<(const NodeId& lhs, const NodeId& rhs) noexcept;

private:
    std::array<std::uint8_t, kBytes> bytes_{};
};

struct NodeDistance {
    std::array<std::uint8_t, NodeId::kBytes> bytes{};
};

[[nodiscard]] NodeDistance xor_distance(const NodeId& lhs, const NodeId& rhs) noexcept;
[[nodiscard]] bool distance_less(const NodeDistance& lhs, const NodeDistance& rhs) noexcept;
[[nodiscard]] std::optional<std::size_t> bucket_index(const NodeId& local, const NodeId& remote) noexcept;

} // namespace tightrope::p2p::dht

