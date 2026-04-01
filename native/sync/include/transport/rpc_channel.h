#pragma once
// Multiplexed RPC over TLS (Raft + sync + CRDT)

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tightrope::sync::transport {

struct RpcFrame {
    std::uint16_t channel = 0;
    std::vector<std::uint8_t> payload;
};

class RpcChannel {
public:
    static std::vector<std::uint8_t> encode(const RpcFrame& frame);
    static std::optional<RpcFrame> try_decode(std::vector<std::uint8_t>& buffer);
};

struct RpcIngressLimits {
    std::size_t max_buffered_bytes = 8U * 1024U * 1024U;
    std::size_t pause_buffered_bytes = 4U * 1024U * 1024U;
    std::size_t resume_buffered_bytes = 2U * 1024U * 1024U;
    std::size_t max_queued_frames = 32U;
    std::size_t max_queued_payload_bytes = 8U * 1024U * 1024U;
    std::size_t max_frame_payload_bytes = 4U * 1024U * 1024U;
};

class RpcIngressQueue final {
public:
    explicit RpcIngressQueue(RpcIngressLimits limits = {});

    bool ingest(std::span<const std::uint8_t> bytes, std::string* error = nullptr);
    bool ingest(const std::vector<std::uint8_t>& bytes, std::string* error = nullptr);
    std::optional<RpcFrame> pop();

    [[nodiscard]] bool has_frame() const noexcept;
    [[nodiscard]] bool should_pause_reads() const noexcept;
    [[nodiscard]] bool should_resume_reads() const noexcept;
    [[nodiscard]] std::size_t buffered_bytes() const noexcept;
    [[nodiscard]] std::size_t queued_frames() const noexcept;
    [[nodiscard]] std::size_t queued_payload_bytes() const noexcept;
    [[nodiscard]] const RpcIngressLimits& limits() const noexcept;

private:
    bool drain_decoded_frames(std::string* error = nullptr);

    RpcIngressLimits limits_{};
    std::vector<std::uint8_t> input_buffer_{};
    std::deque<RpcFrame> queue_{};
    std::size_t queued_payload_bytes_ = 0;
};

} // namespace tightrope::sync::transport
