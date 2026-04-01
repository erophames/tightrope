#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <thread>
#include <unistd.h>

#include <sqlite3.h>

#include "discovery/peer_endpoint.h"
#include "transport/peer_probe.h"
#include "transport/replication_socket_server.h"

namespace {

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-peer-probe-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd != -1);
    close(fd);
    std::remove(path);
    return std::string(path);
}

void exec_sql(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    const auto rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (err != nullptr) {
        INFO(std::string(err));
        sqlite3_free(err);
    }
    REQUIRE(rc == SQLITE_OK);
}

void init_journal(sqlite3* db) {
    exec_sql(db, R"sql(
        CREATE TABLE _sync_journal (
          seq         INTEGER PRIMARY KEY,
          hlc_wall    INTEGER NOT NULL,
          hlc_counter INTEGER NOT NULL,
          site_id     INTEGER NOT NULL,
          table_name  TEXT    NOT NULL,
          row_pk      TEXT    NOT NULL,
          op          TEXT    NOT NULL,
          old_values  TEXT,
          new_values  TEXT,
          checksum    TEXT    NOT NULL,
          applied     INTEGER DEFAULT 1,
          batch_id    TEXT
        );
    )sql");
}

tightrope::sync::ApplyWireBatchRequest default_apply_request() {
    return {
        .remote_handshake = {},
        .cluster_shared_secret = "cluster-secret",
        .require_handshake_auth = true,
        .local_schema_version = 2,
        .allow_schema_downgrade = false,
        .min_supported_schema_version = 2,
        .applied_value = 2,
    };
}

} // namespace

TEST_CASE("peer probe succeeds when remote accepts handshake", "[sync][transport][peer_probe]") {
    const auto target_path = make_temp_db_path();
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(target != nullptr);
    init_journal(target);
    sqlite3_close(target);

    tightrope::sync::transport::ReplicationSocketServerConfig server_config{};
    server_config.bind_host = "127.0.0.1";
    server_config.bind_port = 0;
    server_config.tls_enabled = false;
    server_config.apply_request = default_apply_request();
    server_config.ingress = {
        .rpc_limits = {},
        .handshake_channel = 1,
        .replication_channel = 2,
        .require_initial_handshake = true,
        .reject_unknown_channels = true,
        .max_frames_per_ingest = 8,
    };
    tightrope::sync::transport::ReplicationSocketServer server(target_path, server_config);
    std::string start_error;
    REQUIRE(server.start(&start_error));
    REQUIRE(start_error.empty());
    REQUIRE(server.bound_port() != 0);

    const tightrope::sync::discovery::PeerEndpoint endpoint{
        .host = "127.0.0.1",
        .port = server.bound_port(),
    };
    tightrope::sync::transport::PeerProbeConfig probe_config{};
    probe_config.local_site_id = 77;
    probe_config.local_schema_version = 2;
    probe_config.auth_key_id = "cluster-key-v1";
    probe_config.cluster_shared_secret = "cluster-secret";
    probe_config.require_handshake_auth = true;
    probe_config.tls_enabled = false;
    probe_config.handshake_channel = 1;

    const auto probe = tightrope::sync::transport::probe_peer_handshake(endpoint, probe_config);
    REQUIRE(probe.ok);
    REQUIRE(probe.error.empty());
    REQUIRE(probe.duration_ms <= probe_config.timeout_ms * 4);

    server.stop();
    std::remove(target_path.c_str());
}

TEST_CASE("peer probe fails when remote rejects schema", "[sync][transport][peer_probe]") {
    const auto target_path = make_temp_db_path();
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(target != nullptr);
    init_journal(target);
    sqlite3_close(target);

    tightrope::sync::transport::ReplicationSocketServerConfig server_config{};
    server_config.bind_host = "127.0.0.1";
    server_config.bind_port = 0;
    server_config.tls_enabled = false;
    server_config.apply_request = default_apply_request();
    server_config.ingress = {
        .rpc_limits = {},
        .handshake_channel = 1,
        .replication_channel = 2,
        .require_initial_handshake = true,
        .reject_unknown_channels = true,
        .max_frames_per_ingest = 8,
    };
    tightrope::sync::transport::ReplicationSocketServer server(target_path, server_config);
    std::string start_error;
    REQUIRE(server.start(&start_error));
    REQUIRE(start_error.empty());
    REQUIRE(server.bound_port() != 0);

    const tightrope::sync::discovery::PeerEndpoint endpoint{
        .host = "127.0.0.1",
        .port = server.bound_port(),
    };
    tightrope::sync::transport::PeerProbeConfig probe_config{};
    probe_config.local_site_id = 78;
    probe_config.local_schema_version = 1;
    probe_config.auth_key_id = "cluster-key-v1";
    probe_config.cluster_shared_secret = "cluster-secret";
    probe_config.require_handshake_auth = true;
    probe_config.tls_enabled = false;
    probe_config.handshake_channel = 1;

    const auto probe = tightrope::sync::transport::probe_peer_handshake(endpoint, probe_config);
    REQUIRE_FALSE(probe.ok);
    REQUIRE_FALSE(probe.error.empty());
    REQUIRE(probe.duration_ms <= probe_config.timeout_ms * 4);

    server.stop();
    std::remove(target_path.c_str());
}
