#include <catch2/catch_test_macros.hpp>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "upstream_transport.h"
#include "server.h"
#include "server/runtime_test_utils.h"

namespace {

class RuntimeBridgeProbeUpstream final {
  public:
    RuntimeBridgeProbeUpstream() : acceptor_(io_context_) {
        namespace asio = boost::asio;
        using tcp = asio::ip::tcp;

        boost::system::error_code ec;
        acceptor_.open(tcp::v4(), ec);
        REQUIRE_FALSE(ec);
        acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
        REQUIRE_FALSE(ec);
        acceptor_.bind(tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0), ec);
        REQUIRE_FALSE(ec);
        acceptor_.listen(asio::socket_base::max_listen_connections, ec);
        REQUIRE_FALSE(ec);

        const auto endpoint = acceptor_.local_endpoint(ec);
        REQUIRE_FALSE(ec);
        port_ = endpoint.port();
        REQUIRE(port_ > 0);

        server_thread_ = std::thread([this] { serve_loop(); });
    }

    ~RuntimeBridgeProbeUpstream() {
        stopping_.store(true, std::memory_order_relaxed);
        boost::system::error_code ec;
        acceptor_.close(ec);
        io_context_.stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    [[nodiscard]] std::uint16_t port() const {
        return port_;
    }

    [[nodiscard]] int connection_count() const {
        return connection_count_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] int request_count() const {
        return request_count_.load(std::memory_order_relaxed);
    }

  private:
    void serve_loop() {
        namespace asio = boost::asio;
        namespace websocket = boost::beast::websocket;
        using tcp = asio::ip::tcp;

        while (!stopping_.load(std::memory_order_relaxed)) {
            tcp::socket socket(io_context_);
            boost::system::error_code ec;
            acceptor_.accept(socket, ec);
            if (ec) {
                if (stopping_.load(std::memory_order_relaxed)) {
                    break;
                }
                continue;
            }

            connection_count_.fetch_add(1, std::memory_order_relaxed);
            websocket::stream<tcp::socket> ws(std::move(socket));
            ws.accept(ec);
            if (ec) {
                continue;
            }

            for (;;) {
                boost::beast::flat_buffer buffer;
                ws.read(buffer, ec);
                if (ec == websocket::error::closed) {
                    break;
                }
                if (ec) {
                    break;
                }

                const auto index = request_count_.fetch_add(1, std::memory_order_relaxed) + 1;
                const auto response_id = std::string("resp_runtime_bridge_") + std::to_string(index);
                const auto created = std::string(R"({"type":"response.created","response":{"id":")") + response_id +
                                     R"(","status":"in_progress"}})";
                const auto completed = std::string(R"({"type":"response.completed","response":{"id":")") +
                                       response_id + R"(","object":"response","status":"completed","output":[]}})";

                ws.text(true);
                ws.write(asio::buffer(created), ec);
                if (ec) {
                    break;
                }
                ws.text(true);
                ws.write(asio::buffer(completed), ec);
                if (ec) {
                    break;
                }
            }

            boost::system::error_code close_ec;
            ws.close(websocket::close_code::normal, close_ec);
        }
    }

    boost::asio::io_context io_context_{1};
    boost::asio::ip::tcp::acceptor acceptor_;
    std::thread server_thread_{};
    std::atomic<bool> stopping_{false};
    std::atomic<int> connection_count_{0};
    std::atomic<int> request_count_{0};
    std::uint16_t port_ = 0;
};

class RuntimeWsClient final {
  public:
    RuntimeWsClient(const std::uint16_t port, const std::string_view path, const std::string_view session_id)
        : resolver_(io_context_), ws_(io_context_) {
        namespace asio = boost::asio;
        namespace beast = boost::beast;
        namespace websocket = beast::websocket;
        using tcp = asio::ip::tcp;

        const auto endpoint = resolver_.resolve("127.0.0.1", std::to_string(port));
        beast::get_lowest_layer(ws_).connect(endpoint);
        ws_.set_option(websocket::stream_base::timeout{
            .handshake_timeout = std::chrono::seconds(2),
            .idle_timeout = std::chrono::seconds(2),
            .keep_alive_pings = false,
        });
        ws_.set_option(websocket::stream_base::decorator([session = std::string(session_id)](websocket::request_type& req) {
            req.set(boost::beast::http::field::origin, "codex_cli_rs");
            req.set("session_id", session);
        }));

        ws_.handshake("127.0.0.1:" + std::to_string(port), std::string(path));
    }

    std::vector<std::string> send_and_read(const std::string_view payload, const std::size_t frame_count) {
        namespace asio = boost::asio;
        namespace beast = boost::beast;
        namespace websocket = beast::websocket;

        ws_.text(true);
        ws_.write(asio::buffer(payload.data(), payload.size()));

        std::vector<std::string> frames;
        for (std::size_t index = 0; index < frame_count; ++index) {
            beast::flat_buffer buffer;
            boost::system::error_code ec;
            ws_.read(buffer, ec);
            if (ec == websocket::error::closed) {
                break;
            }
            REQUIRE_FALSE(ec);
            frames.push_back(beast::buffers_to_string(buffer.data()));
        }
        return frames;
    }

    void close() {
        namespace websocket = boost::beast::websocket;
        boost::system::error_code ec;
        ws_.close(websocket::close_code::normal, ec);
    }

  private:
    boost::asio::io_context io_context_{1};
    boost::asio::ip::tcp::resolver resolver_;
    boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
};

class DelayedWebsocketUpstreamTransport final : public tightrope::proxy::UpstreamTransport {
  public:
    tightrope::proxy::UpstreamExecutionResult execute(const tightrope::proxy::openai::UpstreamRequestPlan&) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        return {
            .status = 101,
            .body = {},
            .events =
                {
                    R"({"type":"response.created","response":{"id":"resp_runtime_heartbeat","status":"in_progress"}})",
                    R"({"type":"response.completed","response":{"id":"resp_runtime_heartbeat","object":"response","status":"completed","output":[]}})",
                },
            .headers = {},
            .accepted = true,
            .close_code = 1000,
            .error_code = {},
        };
    }
};

} // namespace

TEST_CASE("uwebsockets runtime serves health and models routes", "[server][runtime]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto health_response = tightrope::tests::server::send_raw_http(
        port,
        "GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(health_response.find("200 OK") != std::string::npos);
    REQUIRE(health_response.find("\"status\":\"ok\"") != std::string::npos);
    REQUIRE(health_response.find("\"uptime_ms\":") != std::string::npos);

    const auto models_response = tightrope::tests::server::send_raw_http(
        port,
        "GET /v1/models HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(models_response.find("200 OK") != std::string::npos);
    REQUIRE(models_response.find("\"object\":\"list\"") != std::string::npos);
    REQUIRE(models_response.find("\"data\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime stop is idempotent", "[server][runtime]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));
    REQUIRE(runtime.stop());
    REQUIRE(runtime.stop());

    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime upgrades websocket responses route", "[server][runtime][ws]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto handshake_response = tightrope::tests::server::send_raw_http(
        port,
        "GET /v1/responses HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n",
        4
    );

    REQUIRE(handshake_response.find("101 Switching Protocols") != std::string::npos);
    const bool has_accept_header = handshake_response.find("Sec-WebSocket-Accept") != std::string::npos ||
                                   handshake_response.find("sec-websocket-accept") != std::string::npos;
    REQUIRE(has_accept_header);
    const auto turn_state = tightrope::tests::server::http_header_value(handshake_response, "x-codex-turn-state");
    REQUIRE(turn_state.has_value());
    REQUIRE(turn_state->rfind("http_turn_", 0) == 0);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime serves firewall CRUD endpoints", "[server][runtime][firewall]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const auto initial = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/firewall/ips HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(initial.find("200 OK") != std::string::npos);
    REQUIRE(initial.find("\"mode\":\"allow_all\"") != std::string::npos);

    const std::string create_body = R"({"ipAddress":"127.0.0.1"})";
    const auto created = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/firewall/ips HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(create_body.size()) + "\r\n\r\n" + create_body
    );
    REQUIRE(created.find("200 OK") != std::string::npos);
    REQUIRE(created.find("\"ipAddress\":\"127.0.0.1\"") != std::string::npos);
    REQUIRE(created.find("\"createdAt\":") != std::string::npos);

    const auto listed = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/firewall/ips HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(listed.find("200 OK") != std::string::npos);
    REQUIRE(listed.find("\"mode\":\"allowlist_active\"") != std::string::npos);
    REQUIRE(listed.find("\"ipAddress\":\"127.0.0.1\"") != std::string::npos);

    const auto deleted = tightrope::tests::server::send_raw_http(
        port,
        "DELETE /api/firewall/ips/127.0.0.1 HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(deleted.find("200 OK") != std::string::npos);
    REQUIRE(deleted.find("\"status\":\"deleted\"") != std::string::npos);

    const auto final_state = tightrope::tests::server::send_raw_http(
        port,
        "GET /api/firewall/ips HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(final_state.find("200 OK") != std::string::npos);
    REQUIRE(final_state.find("\"mode\":\"allow_all\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime enforces firewall on protected routes", "[server][runtime][firewall]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string blocked_ip_body = R"({"ipAddress":"10.20.30.40"})";
    const auto add_blocked = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/firewall/ips HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(blocked_ip_body.size()) + "\r\n\r\n" + blocked_ip_body
    );
    REQUIRE(add_blocked.find("200 OK") != std::string::npos);

    const auto blocked = tightrope::tests::server::send_raw_http(
        port,
        "GET /v1/models HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(blocked.find("403 Forbidden") != std::string::npos);
    REQUIRE(blocked.find("\"ip_forbidden\"") != std::string::npos);

    const std::string loopback_body = R"({"ipAddress":"127.0.0.1"})";
    const auto add_loopback = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/firewall/ips HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(loopback_body.size()) + "\r\n\r\n" + loopback_body
    );
    REQUIRE(add_loopback.find("200 OK") != std::string::npos);

    const auto allowed = tightrope::tests::server::send_raw_http(
        port,
        "GET /v1/models HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    );
    REQUIRE(allowed.find("200 OK") != std::string::npos);
    REQUIRE(allowed.find("\"object\":\"list\"") != std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("uwebsockets runtime blocks websocket upgrades when firewall denies access", "[server][runtime][firewall][ws]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    const std::string create_body = R"({"ipAddress":"10.20.30.40"})";
    const auto created = tightrope::tests::server::send_raw_http(
        port,
        "POST /api/firewall/ips HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: " +
            std::to_string(create_body.size()) + "\r\n\r\n" + create_body
    );
    REQUIRE(created.find("200 OK") != std::string::npos);

    const auto handshake_response = tightrope::tests::server::send_raw_http(
        port,
        "GET /v1/responses HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "\r\n",
        4
    );

    REQUIRE(handshake_response.find("403 Forbidden") != std::string::npos);
    REQUIRE(handshake_response.find("\"ip_forbidden\"") != std::string::npos);
    REQUIRE(handshake_response.find("101 Switching Protocols") == std::string::npos);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime websocket close cancels backend codex upstream bridge session", "[server][runtime][ws][bridge]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    tightrope::tests::server::EnvVarGuard base_url_guard{"TIGHTROPE_UPSTREAM_BASE_URL"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));

    RuntimeBridgeProbeUpstream upstream;
    const auto base_url = std::string("http://127.0.0.1:") + std::to_string(upstream.port());
    REQUIRE(base_url_guard.set(base_url));

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    {
        RuntimeWsClient first_client(port, "/backend-api/codex/responses", "runtime-bridge-cancel-1");
        const auto first_frames =
            first_client.send_and_read(R"({"model":"gpt-5.4","input":"runtime-first-turn"})", 2);
        REQUIRE(first_frames.size() == 2);
        REQUIRE(first_frames.back().find(R"("type":"response.completed")") != std::string::npos);
        first_client.close();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        RuntimeWsClient second_client(port, "/backend-api/codex/responses", "runtime-bridge-cancel-1");
        const auto second_frames =
            second_client.send_and_read(R"({"type":"response.cancel","response_id":"resp_runtime_bridge_1"})", 1);
        REQUIRE(second_frames.size() == 1);
        REQUIRE(second_frames.front().find(R"("type":"error")") != std::string::npos);
        REQUIRE(second_frames.front().find("invalid_request_error") != std::string::npos);
    }

    REQUIRE(upstream.request_count() == 1);

    REQUIRE(runtime.stop());
    std::filesystem::remove(db_path);
}

TEST_CASE("runtime websocket sends in-flight heartbeats during slow upstream turns", "[server][runtime][ws][heartbeat]") {
    tightrope::tests::server::EnvVarGuard db_path_guard{"TIGHTROPE_DB_PATH"};
    tightrope::tests::server::EnvVarGuard heartbeat_guard{"TIGHTROPE_RESPONSES_WS_HEARTBEAT_INTERVAL_MS"};
    const auto db_path = tightrope::tests::server::make_temp_runtime_db_path();
    REQUIRE(db_path_guard.set(db_path));
    REQUIRE(heartbeat_guard.set("500"));

    tightrope::proxy::set_upstream_transport(std::make_shared<DelayedWebsocketUpstreamTransport>());

    tightrope::server::Runtime runtime;
    const auto port = tightrope::tests::server::next_runtime_port();
    REQUIRE(runtime.start(tightrope::server::RuntimeConfig{.host = "127.0.0.1", .port = port}));

    {
        RuntimeWsClient client(port, "/v1/responses", "runtime-heartbeat-1");
        const auto frames = client.send_and_read(R"({"model":"gpt-5.4","input":"slow-turn"})", 2);
        REQUIRE(frames.size() == 2);
        REQUIRE(frames.front().find(R"("type":"response.created")") != std::string::npos);
        REQUIRE(frames.back().find(R"("type":"response.completed")") != std::string::npos);
    }

    REQUIRE(runtime.stop());
    tightrope::proxy::reset_upstream_transport();
    std::filesystem::remove(db_path);
}
