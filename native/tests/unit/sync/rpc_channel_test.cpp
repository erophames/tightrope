#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "transport/rpc_channel.h"

TEST_CASE("rpc channel encodes and decodes frames", "[sync][transport][rpc]") {
    const tightrope::sync::transport::RpcFrame frame = {
        .channel = 3,
        .payload = std::vector<std::uint8_t>{'o', 'k'},
    };

    auto bytes = tightrope::sync::transport::RpcChannel::encode(frame);
    const auto decoded = tightrope::sync::transport::RpcChannel::try_decode(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->channel == frame.channel);
    REQUIRE(decoded->payload == frame.payload);
    REQUIRE(bytes.empty());
}

TEST_CASE("rpc channel keeps partial frame buffered", "[sync][transport][rpc]") {
    const tightrope::sync::transport::RpcFrame frame = {
        .channel = 9,
        .payload = std::vector<std::uint8_t>{'x', 'y', 'z'},
    };
    auto bytes = tightrope::sync::transport::RpcChannel::encode(frame);
    bytes.pop_back();

    const auto decoded = tightrope::sync::transport::RpcChannel::try_decode(bytes);
    REQUIRE_FALSE(decoded.has_value());
    REQUIRE_FALSE(bytes.empty());
}

TEST_CASE("rpc channel encodes in little-endian byte order", "[sync][transport][rpc]") {
    const tightrope::sync::transport::RpcFrame frame = {
        .channel = 0x0102,
        .payload = std::vector<std::uint8_t>{'A'},
    };

    const auto bytes = tightrope::sync::transport::RpcChannel::encode(frame);
    REQUIRE(bytes.size() == 7);

    REQUIRE(bytes[0] == 0x02);
    REQUIRE(bytes[1] == 0x01);

    REQUIRE(bytes[2] == 0x01);
    REQUIRE(bytes[3] == 0x00);
    REQUIRE(bytes[4] == 0x00);
    REQUIRE(bytes[5] == 0x00);

    REQUIRE(bytes[6] == 'A');
}

TEST_CASE("rpc channel encodes multi-byte payload size in little-endian", "[sync][transport][rpc]") {
    std::vector<std::uint8_t> payload(258, 0x42); // 258 => 0x00000102
    const tightrope::sync::transport::RpcFrame frame = {
        .channel = 1,
        .payload = payload,
    };

    const auto bytes = tightrope::sync::transport::RpcChannel::encode(frame);
    REQUIRE(bytes[2] == 0x02);
    REQUIRE(bytes[3] == 0x01);
    REQUIRE(bytes[4] == 0x00);
    REQUIRE(bytes[5] == 0x00);
}

TEST_CASE("rpc ingress queue pauses and resumes reads by queued-frame watermarks", "[sync][transport][rpc]") {
    tightrope::sync::transport::RpcIngressQueue queue({
        .max_buffered_bytes = 1024,
        .pause_buffered_bytes = 512,
        .resume_buffered_bytes = 128,
        .max_queued_frames = 2,
        .max_queued_payload_bytes = 1024,
        .max_frame_payload_bytes = 512,
    });

    const auto first = tightrope::sync::transport::RpcChannel::encode({
        .channel = 1,
        .payload = std::vector<std::uint8_t>{'a'},
    });
    const auto second = tightrope::sync::transport::RpcChannel::encode({
        .channel = 2,
        .payload = std::vector<std::uint8_t>{'b'},
    });
    std::vector<std::uint8_t> combined;
    combined.insert(combined.end(), first.begin(), first.end());
    combined.insert(combined.end(), second.begin(), second.end());

    std::string error;
    REQUIRE(queue.ingest(combined, &error));
    REQUIRE(error.empty());
    REQUIRE(queue.queued_frames() == 2);
    REQUIRE(queue.should_pause_reads());

    const auto popped = queue.pop();
    REQUIRE(popped.has_value());
    REQUIRE(popped->channel == 1);
    REQUIRE(queue.queued_frames() == 1);
    REQUIRE(queue.should_resume_reads());
}

TEST_CASE("rpc ingress queue rejects oversized frame payload", "[sync][transport][rpc]") {
    tightrope::sync::transport::RpcIngressQueue queue({
        .max_buffered_bytes = 1024,
        .pause_buffered_bytes = 512,
        .resume_buffered_bytes = 128,
        .max_queued_frames = 4,
        .max_queued_payload_bytes = 1024,
        .max_frame_payload_bytes = 3,
    });

    const auto encoded = tightrope::sync::transport::RpcChannel::encode({
        .channel = 9,
        .payload = std::vector<std::uint8_t>{'t', 'o', 'o', '!', '!'},
    });

    std::string error;
    REQUIRE_FALSE(queue.ingest(encoded, &error));
    REQUIRE(error.find("payload exceeds limit") != std::string::npos);
    REQUIRE(queue.queued_frames() == 0);
}

TEST_CASE("rpc ingress queue retains undecoded frame when queue is full", "[sync][transport][rpc]") {
    tightrope::sync::transport::RpcIngressQueue queue({
        .max_buffered_bytes = 1024,
        .pause_buffered_bytes = 512,
        .resume_buffered_bytes = 128,
        .max_queued_frames = 1,
        .max_queued_payload_bytes = 1024,
        .max_frame_payload_bytes = 512,
    });

    const auto first = tightrope::sync::transport::RpcChannel::encode({
        .channel = 1,
        .payload = std::vector<std::uint8_t>{'1'},
    });
    const auto second = tightrope::sync::transport::RpcChannel::encode({
        .channel = 2,
        .payload = std::vector<std::uint8_t>{'2'},
    });
    std::vector<std::uint8_t> combined;
    combined.insert(combined.end(), first.begin(), first.end());
    combined.insert(combined.end(), second.begin(), second.end());

    REQUIRE(queue.ingest(combined));
    REQUIRE(queue.queued_frames() == 1);
    REQUIRE(queue.buffered_bytes() > 0);
    REQUIRE(queue.should_pause_reads());

    const auto popped = queue.pop();
    REQUIRE(popped.has_value());
    REQUIRE(popped->channel == 1);
    REQUIRE(queue.queued_frames() == 1);

    const auto second_popped = queue.pop();
    REQUIRE(second_popped.has_value());
    REQUIRE(second_popped->channel == 2);
    REQUIRE(queue.buffered_bytes() == 0);
}

TEST_CASE("rpc ingress queue rejects chunk that exceeds buffer budget", "[sync][transport][rpc]") {
    tightrope::sync::transport::RpcIngressQueue queue({
        .max_buffered_bytes = 8,
        .pause_buffered_bytes = 8,
        .resume_buffered_bytes = 4,
        .max_queued_frames = 2,
        .max_queued_payload_bytes = 1024,
        .max_frame_payload_bytes = 512,
    });

    std::vector<std::uint8_t> chunk(9, 0xAB);
    std::string error;
    REQUIRE_FALSE(queue.ingest(chunk, &error));
    REQUIRE(error.find("buffer budget exceeded") != std::string::npos);
}
