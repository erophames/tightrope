#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "sync_protocol.h"

TEST_CASE("sync protocol handshake frame round-trips", "[sync][protocol]") {
    const tightrope::sync::HandshakeFrame handshake = {
        .site_id = 7,
        .schema_version = 3,
        .last_recv_seq_from_peer = 42,
    };

    const auto encoded = tightrope::sync::encode_handshake(handshake);
    REQUIRE(encoded.size() == 16);
    const auto decoded = tightrope::sync::decode_handshake(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->site_id == handshake.site_id);
    REQUIRE(decoded->schema_version == handshake.schema_version);
    REQUIRE(decoded->last_recv_seq_from_peer == handshake.last_recv_seq_from_peer);
    REQUIRE(decoded->auth_key_id.empty());
    REQUIRE(decoded->auth_hmac_hex.empty());
}

TEST_CASE("sync protocol handshake auth signs and validates with shared secret", "[sync][protocol]") {
    tightrope::sync::HandshakeFrame handshake = {
        .site_id = 7,
        .schema_version = 3,
        .last_recv_seq_from_peer = 42,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(handshake, "cluster-secret");
    REQUIRE_FALSE(handshake.auth_hmac_hex.empty());

    const auto encoded = tightrope::sync::encode_handshake(handshake);
    const auto decoded = tightrope::sync::decode_handshake(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->auth_key_id == "cluster-key-v1");
    REQUIRE_FALSE(decoded->auth_hmac_hex.empty());

    const auto validation =
        tightrope::sync::validate_handshake_auth(*decoded, "cluster-secret", /*require_auth=*/true);
    REQUIRE(validation.accepted);
    REQUIRE(validation.error.empty());
}

TEST_CASE("sync protocol handshake auth rejects tampered payload", "[sync][protocol]") {
    tightrope::sync::HandshakeFrame handshake = {
        .site_id = 7,
        .schema_version = 3,
        .last_recv_seq_from_peer = 42,
        .auth_key_id = "cluster-key-v1",
    };
    tightrope::sync::sign_handshake(handshake, "cluster-secret");
    handshake.schema_version = 4;

    const auto validation =
        tightrope::sync::validate_handshake_auth(handshake, "cluster-secret", /*require_auth=*/true);
    REQUIRE_FALSE(validation.accepted);
    REQUIRE(validation.error.find("mismatch") != std::string::npos);
}

TEST_CASE("sync protocol handshake auth rejects missing hmac when required", "[sync][protocol]") {
    const tightrope::sync::HandshakeFrame handshake = {
        .site_id = 7,
        .schema_version = 3,
        .last_recv_seq_from_peer = 42,
    };

    const auto validation =
        tightrope::sync::validate_handshake_auth(handshake, "cluster-secret", /*require_auth=*/true);
    REQUIRE_FALSE(validation.accepted);
    REQUIRE(validation.error.find("missing hmac") != std::string::npos);
}

TEST_CASE("sync protocol handshake schema validation rejects mismatches in strict mode", "[sync][protocol]") {
    const tightrope::sync::HandshakeFrame remote = {
        .site_id = 9,
        .schema_version = 2,
        .last_recv_seq_from_peer = 0,
    };

    const auto validation = tightrope::sync::validate_handshake_schema_version(
        remote,
        /*local_schema_version=*/3,
        /*allow_downgrade=*/false,
        /*min_supported_schema_version=*/1);

    REQUIRE_FALSE(validation.accepted);
    REQUIRE(validation.negotiated_schema_version == 0);
    REQUIRE(validation.error.find("schema version mismatch") != std::string::npos);
}

TEST_CASE("sync protocol handshake schema validation can negotiate down", "[sync][protocol]") {
    const tightrope::sync::HandshakeFrame remote = {
        .site_id = 9,
        .schema_version = 2,
        .last_recv_seq_from_peer = 0,
    };

    const auto validation = tightrope::sync::validate_handshake_schema_version(
        remote,
        /*local_schema_version=*/3,
        /*allow_downgrade=*/true,
        /*min_supported_schema_version=*/1);

    REQUIRE(validation.accepted);
    REQUIRE(validation.negotiated_schema_version == 2);
    REQUIRE(validation.error.empty());
}

TEST_CASE("sync protocol handshake schema validation rejects versions below minimum", "[sync][protocol]") {
    const tightrope::sync::HandshakeFrame remote = {
        .site_id = 9,
        .schema_version = 1,
        .last_recv_seq_from_peer = 0,
    };

    const auto validation = tightrope::sync::validate_handshake_schema_version(
        remote,
        /*local_schema_version=*/3,
        /*allow_downgrade=*/true,
        /*min_supported_schema_version=*/2);

    REQUIRE_FALSE(validation.accepted);
    REQUIRE(validation.negotiated_schema_version == 0);
    REQUIRE(validation.error.find("below minimum supported") != std::string::npos);
}

TEST_CASE("sync protocol journal batch round-trips with compression", "[sync][protocol]") {
    tightrope::sync::JournalBatchFrame batch;
    batch.from_seq = 10;
    batch.to_seq = 11;
    batch.entries = {
        {
            .seq = 11,
            .hlc_wall = 1000,
            .hlc_counter = 1,
            .site_id = 7,
            .table_name = "accounts",
            .row_pk = R"({"id":"1"})",
            .op = "INSERT",
            .old_values = "",
            .new_values = R"({"email":"a@x.com"})",
            .checksum = "abcd",
            .applied = 1,
            .batch_id = "b1",
        },
    };

    const auto encoded = tightrope::sync::encode_journal_batch(batch);
    const auto decoded = tightrope::sync::decode_journal_batch(encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->from_seq == batch.from_seq);
    REQUIRE(decoded->to_seq == batch.to_seq);
    REQUIRE(decoded->entries.size() == 1);
    REQUIRE(decoded->entries.front().row_pk == R"({"id":"1"})");
    REQUIRE(decoded->entries.front().new_values == R"({"email":"a@x.com"})");
}
