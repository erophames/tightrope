#include "transport/rpc_channel.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include <boost/asio/buffer.hpp>

#include "sync_logging.h"

namespace tightrope::sync::transport {

namespace {

constexpr std::size_t kHeaderSize = sizeof(std::uint16_t) + sizeof(std::uint32_t);

void write_u16(std::vector<std::uint8_t>& out, const std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void write_u32(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

std::uint16_t read_u16(const std::uint8_t* raw) {
    return static_cast<std::uint16_t>(raw[0]) | (static_cast<std::uint16_t>(raw[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* raw) {
    return static_cast<std::uint32_t>(raw[0]) | (static_cast<std::uint32_t>(raw[1]) << 8U) |
           (static_cast<std::uint32_t>(raw[2]) << 16U) | (static_cast<std::uint32_t>(raw[3]) << 24U);
}

std::size_t clamp_min(const std::size_t value, const std::size_t min_value) {
    return value < min_value ? min_value : value;
}

void set_error(std::string* error, const std::string_view message) {
    if (error != nullptr) {
        *error = std::string(message);
    }
}

} // namespace

std::vector<std::uint8_t> RpcChannel::encode(const RpcFrame& frame) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kHeaderSize + frame.payload.size());
    write_u16(bytes, frame.channel);
    write_u32(bytes, static_cast<std::uint32_t>(frame.payload.size()));
    bytes.insert(bytes.end(), frame.payload.begin(), frame.payload.end());
    log_sync_event(
        SyncLogLevel::Trace,
        "rpc_channel",
        "encode",
        "channel=" + std::to_string(frame.channel) + " payload_bytes=" + std::to_string(frame.payload.size()));
    return bytes;
}

std::optional<RpcFrame> RpcChannel::try_decode(std::vector<std::uint8_t>& buffer) {
    if (buffer.size() < kHeaderSize) {
        return std::nullopt;
    }

    const auto* raw = buffer.data();
    const auto channel = read_u16(raw);
    const auto payload_size = read_u32(raw + sizeof(std::uint16_t));
    const auto required = kHeaderSize + static_cast<std::size_t>(payload_size);
    if (buffer.size() < required) {
        return std::nullopt;
    }

    RpcFrame frame;
    frame.channel = channel;
    frame.payload.resize(payload_size);
    boost::asio::buffer_copy(
        boost::asio::buffer(frame.payload),
        boost::asio::buffer(raw + kHeaderSize, static_cast<std::size_t>(payload_size))
    );

    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(required));
    log_sync_event(
        SyncLogLevel::Trace,
        "rpc_channel",
        "decode",
        "channel=" + std::to_string(frame.channel) + " payload_bytes=" + std::to_string(frame.payload.size()) +
            " remaining_bytes=" + std::to_string(buffer.size()));
    return frame;
}

RpcIngressQueue::RpcIngressQueue(RpcIngressLimits limits) : limits_(limits) {
    limits_.max_buffered_bytes = clamp_min(limits_.max_buffered_bytes, kHeaderSize);
    limits_.max_frame_payload_bytes = clamp_min(limits_.max_frame_payload_bytes, 1U);
    limits_.max_queued_frames = clamp_min(limits_.max_queued_frames, 1U);
    limits_.max_queued_payload_bytes = clamp_min(limits_.max_queued_payload_bytes, 1U);
    limits_.pause_buffered_bytes = std::min(limits_.pause_buffered_bytes, limits_.max_buffered_bytes);
    limits_.resume_buffered_bytes = std::min(limits_.resume_buffered_bytes, limits_.pause_buffered_bytes);
}

bool RpcIngressQueue::ingest(const std::span<const std::uint8_t> bytes, std::string* error) {
    if (bytes.size() > (limits_.max_buffered_bytes - std::min(input_buffer_.size(), limits_.max_buffered_bytes))) {
        set_error(
            error,
            "rpc ingress buffer budget exceeded (incoming=" + std::to_string(bytes.size()) + ", buffered=" +
                std::to_string(input_buffer_.size()) + ", max=" + std::to_string(limits_.max_buffered_bytes) + ")");
        return false;
    }
    if (!bytes.empty()) {
        input_buffer_.insert(input_buffer_.end(), bytes.begin(), bytes.end());
    }
    return drain_decoded_frames(error);
}

bool RpcIngressQueue::ingest(const std::vector<std::uint8_t>& bytes, std::string* error) {
    return ingest(std::span<const std::uint8_t>(bytes.data(), bytes.size()), error);
}

std::optional<RpcFrame> RpcIngressQueue::pop() {
    if (queue_.empty()) {
        return std::nullopt;
    }
    RpcFrame frame = std::move(queue_.front());
    queue_.pop_front();
    if (queued_payload_bytes_ >= frame.payload.size()) {
        queued_payload_bytes_ -= frame.payload.size();
    } else {
        queued_payload_bytes_ = 0;
    }
    (void)drain_decoded_frames(nullptr);
    return frame;
}

bool RpcIngressQueue::has_frame() const noexcept {
    return !queue_.empty();
}

bool RpcIngressQueue::should_pause_reads() const noexcept {
    return input_buffer_.size() >= limits_.pause_buffered_bytes || queue_.size() >= limits_.max_queued_frames ||
           queued_payload_bytes_ >= limits_.max_queued_payload_bytes;
}

bool RpcIngressQueue::should_resume_reads() const noexcept {
    return input_buffer_.size() <= limits_.resume_buffered_bytes && queue_.size() < limits_.max_queued_frames &&
           queued_payload_bytes_ < limits_.max_queued_payload_bytes;
}

std::size_t RpcIngressQueue::buffered_bytes() const noexcept {
    return input_buffer_.size();
}

std::size_t RpcIngressQueue::queued_frames() const noexcept {
    return queue_.size();
}

std::size_t RpcIngressQueue::queued_payload_bytes() const noexcept {
    return queued_payload_bytes_;
}

const RpcIngressLimits& RpcIngressQueue::limits() const noexcept {
    return limits_;
}

bool RpcIngressQueue::drain_decoded_frames(std::string* error) {
    while (!input_buffer_.empty()) {
        if (queue_.size() >= limits_.max_queued_frames) {
            break;
        }
        if (input_buffer_.size() < kHeaderSize) {
            break;
        }

        const auto payload_size = static_cast<std::size_t>(read_u32(input_buffer_.data() + sizeof(std::uint16_t)));
        if (payload_size > limits_.max_frame_payload_bytes) {
            set_error(
                error,
                "rpc ingress frame payload exceeds limit (payload_bytes=" + std::to_string(payload_size) + ", max=" +
                    std::to_string(limits_.max_frame_payload_bytes) + ")");
            return false;
        }

        const auto required = kHeaderSize + payload_size;
        if (required > limits_.max_buffered_bytes) {
            set_error(
                error,
                "rpc ingress frame size exceeds buffer limit (frame_bytes=" + std::to_string(required) + ", max=" +
                    std::to_string(limits_.max_buffered_bytes) + ")");
            return false;
        }
        if (input_buffer_.size() < required) {
            break;
        }
        if (queued_payload_bytes_ > (limits_.max_queued_payload_bytes - payload_size)) {
            break;
        }

        auto decoded = RpcChannel::try_decode(input_buffer_);
        if (!decoded.has_value()) {
            break;
        }
        queued_payload_bytes_ += decoded->payload.size();
        queue_.push_back(std::move(*decoded));
    }
    return true;
}

} // namespace tightrope::sync::transport
