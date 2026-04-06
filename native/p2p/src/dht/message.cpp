#include "p2p/dht/message.h"

#include <array>
#include <cstddef>
#include <cstring>

namespace tightrope::p2p::dht {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'T', 'D', 'H', 'T'};
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kMaxPacketBytes = 4096;
constexpr std::size_t kMaxNodesPerMessage = 32;
constexpr std::size_t kMaxValuesPerMessage = 32;
constexpr std::size_t kMaxHostBytes = 255;
constexpr std::size_t kMaxKeyBytes = 256;
constexpr std::size_t kMaxValueBytes = 1024;
constexpr std::size_t kMaxErrorBytes = 512;

class BufferWriter {
public:
    void u8(const std::uint8_t value) {
        out_.push_back(value);
    }

    void u16(const std::uint16_t value) {
        out_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFF));
        out_.push_back(static_cast<std::uint8_t>(value & 0xFF));
    }

    void u32(const std::uint32_t value) {
        out_.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFF));
        out_.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFF));
        out_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFF));
        out_.push_back(static_cast<std::uint8_t>(value & 0xFF));
    }

    void u64(const std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            out_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
        }
    }

    void bytes(std::span<const std::uint8_t> data) {
        out_.insert(out_.end(), data.begin(), data.end());
    }

    bool string_u8(std::string_view value) {
        if (value.size() > 0xFF) {
            return false;
        }
        u8(static_cast<std::uint8_t>(value.size()));
        out_.insert(out_.end(), value.begin(), value.end());
        return true;
    }

    bool string_u16(std::string_view value) {
        if (value.size() > 0xFFFF) {
            return false;
        }
        u16(static_cast<std::uint16_t>(value.size()));
        out_.insert(out_.end(), value.begin(), value.end());
        return true;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& data() const noexcept { return out_; }

private:
    std::vector<std::uint8_t> out_;
};

class BufferReader {
public:
    explicit BufferReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    bool u8(std::uint8_t* out) {
        if (out == nullptr || remaining() < 1) {
            return false;
        }
        *out = bytes_[offset_++];
        return true;
    }

    bool u16(std::uint16_t* out) {
        if (out == nullptr || remaining() < 2) {
            return false;
        }
        *out = static_cast<std::uint16_t>((bytes_[offset_] << 8U) | bytes_[offset_ + 1]);
        offset_ += 2;
        return true;
    }

    bool u32(std::uint32_t* out) {
        if (out == nullptr || remaining() < 4) {
            return false;
        }
        *out = (static_cast<std::uint32_t>(bytes_[offset_]) << 24U) |
               (static_cast<std::uint32_t>(bytes_[offset_ + 1]) << 16U) |
               (static_cast<std::uint32_t>(bytes_[offset_ + 2]) << 8U) |
               static_cast<std::uint32_t>(bytes_[offset_ + 3]);
        offset_ += 4;
        return true;
    }

    bool u64(std::uint64_t* out) {
        if (out == nullptr || remaining() < 8) {
            return false;
        }
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i) {
            value = (value << 8U) | bytes_[offset_ + static_cast<std::size_t>(i)];
        }
        offset_ += 8;
        *out = value;
        return true;
    }

    bool bytes(std::size_t size, std::span<const std::uint8_t>* out) {
        if (out == nullptr || remaining() < size) {
            return false;
        }
        *out = bytes_.subspan(offset_, size);
        offset_ += size;
        return true;
    }

    bool string_u8(std::string* out) {
        if (out == nullptr) {
            return false;
        }
        std::uint8_t size = 0;
        if (!u8(&size) || remaining() < size) {
            return false;
        }
        out->assign(
            reinterpret_cast<const char*>(bytes_.data() + static_cast<std::ptrdiff_t>(offset_)),
            static_cast<std::size_t>(size)
        );
        offset_ += size;
        return true;
    }

    bool string_u16(std::string* out) {
        if (out == nullptr) {
            return false;
        }
        std::uint16_t size = 0;
        if (!u16(&size) || remaining() < size) {
            return false;
        }
        out->assign(
            reinterpret_cast<const char*>(bytes_.data() + static_cast<std::ptrdiff_t>(offset_)),
            static_cast<std::size_t>(size)
        );
        offset_ += size;
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

void write_header(BufferWriter* writer, const RpcMessage& message) {
    writer->bytes(kMagic);
    writer->u8(kVersion);
    writer->u8(static_cast<std::uint8_t>(message.type));
    writer->u32(message.tx_id);
    writer->u16(message.source_port);
    writer->bytes(message.sender_id.bytes());
}

bool read_header(BufferReader* reader, RpcMessage* message) {
    if (reader == nullptr || message == nullptr) {
        return false;
    }
    std::span<const std::uint8_t> magic{};
    if (!reader->bytes(kMagic.size(), &magic)) {
        return false;
    }
    if (!std::equal(magic.begin(), magic.end(), kMagic.begin(), kMagic.end())) {
        return false;
    }

    std::uint8_t version = 0;
    std::uint8_t raw_type = 0;
    if (!reader->u8(&version) || !reader->u8(&raw_type) || version != kVersion) {
        return false;
    }
    message->type = static_cast<RpcType>(raw_type);

    if (!reader->u32(&message->tx_id) || !reader->u16(&message->source_port)) {
        return false;
    }

    std::span<const std::uint8_t> sender_bytes{};
    if (!reader->bytes(NodeId::kBytes, &sender_bytes)) {
        return false;
    }
    std::array<std::uint8_t, NodeId::kBytes> raw{};
    std::memcpy(raw.data(), sender_bytes.data(), raw.size());
    message->sender_id = NodeId{raw};
    return true;
}

bool write_node_contact(BufferWriter* writer, const NodeWireContact& contact) {
    if (writer == nullptr || contact.endpoint.host.empty() || contact.endpoint.host.size() > kMaxHostBytes ||
        !is_valid_endpoint(contact.endpoint)) {
        return false;
    }
    writer->bytes(contact.id.bytes());
    if (!writer->string_u8(contact.endpoint.host)) {
        return false;
    }
    writer->u16(contact.endpoint.port);
    writer->u64(contact.last_seen_unix_ms);
    return true;
}

bool read_node_contact(BufferReader* reader, NodeWireContact* contact) {
    if (reader == nullptr || contact == nullptr) {
        return false;
    }
    std::span<const std::uint8_t> node_id_bytes{};
    if (!reader->bytes(NodeId::kBytes, &node_id_bytes)) {
        return false;
    }
    std::array<std::uint8_t, NodeId::kBytes> raw{};
    std::memcpy(raw.data(), node_id_bytes.data(), raw.size());
    contact->id = NodeId{raw};

    if (!reader->string_u8(&contact->endpoint.host)) {
        return false;
    }
    if (!reader->u16(&contact->endpoint.port)) {
        return false;
    }
    if (!reader->u64(&contact->last_seen_unix_ms)) {
        return false;
    }
    return is_valid_endpoint(contact->endpoint);
}

bool write_values(BufferWriter* writer, const std::vector<ValueRecord>& values) {
    if (writer == nullptr || values.size() > kMaxValuesPerMessage) {
        return false;
    }
    writer->u16(static_cast<std::uint16_t>(values.size()));
    for (const auto& entry : values) {
        if (entry.value.size() > kMaxValueBytes) {
            return false;
        }
        if (!writer->string_u16(entry.value)) {
            return false;
        }
        writer->u64(entry.expires_unix_ms);
    }
    return true;
}

bool read_values(BufferReader* reader, std::vector<ValueRecord>* values) {
    if (reader == nullptr || values == nullptr) {
        return false;
    }
    std::uint16_t count = 0;
    if (!reader->u16(&count) || count > kMaxValuesPerMessage) {
        return false;
    }
    values->clear();
    values->reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        ValueRecord entry{};
        if (!reader->string_u16(&entry.value)) {
            return false;
        }
        if (entry.value.size() > kMaxValueBytes || !reader->u64(&entry.expires_unix_ms)) {
            return false;
        }
        values->push_back(std::move(entry));
    }
    return true;
}

bool write_nodes(BufferWriter* writer, const std::vector<NodeWireContact>& nodes) {
    if (writer == nullptr || nodes.size() > kMaxNodesPerMessage) {
        return false;
    }
    writer->u16(static_cast<std::uint16_t>(nodes.size()));
    for (const auto& node : nodes) {
        if (!write_node_contact(writer, node)) {
            return false;
        }
    }
    return true;
}

bool read_nodes(BufferReader* reader, std::vector<NodeWireContact>* nodes) {
    if (reader == nullptr || nodes == nullptr) {
        return false;
    }
    std::uint16_t count = 0;
    if (!reader->u16(&count) || count > kMaxNodesPerMessage) {
        return false;
    }
    nodes->clear();
    nodes->reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        NodeWireContact contact{};
        if (!read_node_contact(reader, &contact)) {
            return false;
        }
        nodes->push_back(std::move(contact));
    }
    return true;
}

} // namespace

std::optional<std::vector<std::uint8_t>> encode_message(const RpcMessage& message) {
    BufferWriter writer;
    write_header(&writer, message);

    switch (message.type) {
    case RpcType::kPingRequest:
        break;
    case RpcType::kPingResponse:
        writer.u64(message.ping_unix_ms);
        break;
    case RpcType::kFindNodeRequest:
        writer.bytes(message.target_id.bytes());
        writer.u16(message.max_results);
        break;
    case RpcType::kFindNodeResponse:
        if (!write_nodes(&writer, message.nodes)) {
            return std::nullopt;
        }
        break;
    case RpcType::kAnnouncePeerRequest:
        if (message.key.empty() || message.key.size() > kMaxKeyBytes || message.value.size() > kMaxValueBytes) {
            return std::nullopt;
        }
        if (!writer.string_u16(message.key) || !writer.string_u16(message.value)) {
            return std::nullopt;
        }
        writer.u32(message.ttl_seconds);
        break;
    case RpcType::kAnnouncePeerResponse:
        writer.u8(static_cast<std::uint8_t>(message.accepted ? 1 : 0));
        break;
    case RpcType::kFindValueRequest:
        if (message.key.empty() || message.key.size() > kMaxKeyBytes) {
            return std::nullopt;
        }
        if (!writer.string_u16(message.key)) {
            return std::nullopt;
        }
        writer.u16(message.max_results);
        break;
    case RpcType::kFindValueResponse:
        if (!write_values(&writer, message.values) || !write_nodes(&writer, message.nodes)) {
            return std::nullopt;
        }
        break;
    case RpcType::kError:
        if (message.error_message.size() > kMaxErrorBytes) {
            return std::nullopt;
        }
        writer.u16(message.error_code);
        if (!writer.string_u16(message.error_message)) {
            return std::nullopt;
        }
        break;
    default:
        return std::nullopt;
    }

    if (writer.data().empty() || writer.data().size() > kMaxPacketBytes) {
        return std::nullopt;
    }
    return writer.data();
}

std::optional<RpcMessage> decode_message(std::span<const std::uint8_t> payload) {
    if (payload.empty() || payload.size() > kMaxPacketBytes) {
        return std::nullopt;
    }

    BufferReader reader(payload);
    RpcMessage message{};
    if (!read_header(&reader, &message)) {
        return std::nullopt;
    }

    switch (message.type) {
    case RpcType::kPingRequest:
        break;
    case RpcType::kPingResponse: {
        if (!reader.u64(&message.ping_unix_ms)) {
            return std::nullopt;
        }
        break;
    }
    case RpcType::kFindNodeRequest: {
        std::span<const std::uint8_t> target_bytes{};
        if (!reader.bytes(NodeId::kBytes, &target_bytes) || !reader.u16(&message.max_results)) {
            return std::nullopt;
        }
        std::array<std::uint8_t, NodeId::kBytes> raw{};
        std::memcpy(raw.data(), target_bytes.data(), raw.size());
        message.target_id = NodeId{raw};
        break;
    }
    case RpcType::kFindNodeResponse:
        if (!read_nodes(&reader, &message.nodes)) {
            return std::nullopt;
        }
        break;
    case RpcType::kAnnouncePeerRequest:
        if (!reader.string_u16(&message.key) || !reader.string_u16(&message.value) || !reader.u32(&message.ttl_seconds) ||
            message.key.empty() || message.key.size() > kMaxKeyBytes || message.value.size() > kMaxValueBytes) {
            return std::nullopt;
        }
        break;
    case RpcType::kAnnouncePeerResponse: {
        std::uint8_t accepted = 0;
        if (!reader.u8(&accepted)) {
            return std::nullopt;
        }
        message.accepted = accepted != 0;
        break;
    }
    case RpcType::kFindValueRequest:
        if (!reader.string_u16(&message.key) || !reader.u16(&message.max_results) || message.key.empty() ||
            message.key.size() > kMaxKeyBytes) {
            return std::nullopt;
        }
        break;
    case RpcType::kFindValueResponse:
        if (!read_values(&reader, &message.values) || !read_nodes(&reader, &message.nodes)) {
            return std::nullopt;
        }
        break;
    case RpcType::kError:
        if (!reader.u16(&message.error_code) || !reader.string_u16(&message.error_message) ||
            message.error_message.size() > kMaxErrorBytes) {
            return std::nullopt;
        }
        break;
    default:
        return std::nullopt;
    }

    if (reader.remaining() != 0) {
        return std::nullopt;
    }
    return message;
}

const char* rpc_type_name(const RpcType type) {
    switch (type) {
    case RpcType::kPingRequest:
        return "ping_req";
    case RpcType::kPingResponse:
        return "ping_res";
    case RpcType::kFindNodeRequest:
        return "find_node_req";
    case RpcType::kFindNodeResponse:
        return "find_node_res";
    case RpcType::kAnnouncePeerRequest:
        return "announce_req";
    case RpcType::kAnnouncePeerResponse:
        return "announce_res";
    case RpcType::kFindValueRequest:
        return "find_value_req";
    case RpcType::kFindValueResponse:
        return "find_value_res";
    case RpcType::kError:
        return "error";
    default:
        return "unknown";
    }
}

bool is_request_type(const RpcType type) noexcept {
    return type == RpcType::kPingRequest || type == RpcType::kFindNodeRequest || type == RpcType::kAnnouncePeerRequest ||
           type == RpcType::kFindValueRequest;
}

bool is_response_type(const RpcType type) noexcept {
    return type == RpcType::kPingResponse || type == RpcType::kFindNodeResponse ||
           type == RpcType::kAnnouncePeerResponse || type == RpcType::kFindValueResponse || type == RpcType::kError;
}

} // namespace tightrope::p2p::dht
