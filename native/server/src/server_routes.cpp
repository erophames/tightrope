#include "internal/server_routes.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <glaze/glaze.hpp>

#include "internal/admin_runtime.h"
#include "internal/firewall_runtime.h"
#include "internal/http_runtime_utils.h"
#include "internal/async_executor.h"
#include "internal/proxy_request_logger.h"
#include "internal/proxy_runtime_control.h"
#include "internal/proxy_usage_reservation.h"
#include "controllers/health_controller.h"
#include "controllers/proxy_controller.h"
#include "controllers/controller_db.h"
#include "controllers/usage_controller.h"
#include "internal/proxy_service_helpers.h"
#include "logging/logger.h"
#include "openai/error_envelope.h"
#include "openai/provider_contract.h"
#include "middleware/api_key_filter.h"
#include "middleware/decompression.h"
#include "middleware/request_id.h"
#include "session/http_bridge.h"
#include "server.h"
#include "upstream_transport.h"

namespace tightrope::server::internal {

namespace {

using proxy::openai::HeaderMap;
using Json = glz::generic;

struct WsRequestContext {
    std::string route;
    HeaderMap headers;
    std::string request_id;
    middleware::ApiKeyModelPolicy api_key_policy;
    std::uint64_t message_sequence = 0;
    std::string connection_id;
    std::shared_ptr<std::mutex> message_mutex;
};

struct RequestContext {
    HeaderMap headers;
    std::string request_id;
    middleware::ApiKeyModelPolicy api_key_policy;
};

constexpr std::size_t kResponsesWsMaxPayloadBytes = 16u * 1024u * 1024u;

std::string current_unix_ms_string() {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    return std::to_string(now.time_since_epoch().count());
}

std::optional<std::string> json_string(const Json::object_t& payload, const std::string_view key) {
    const auto it = payload.find(std::string(key));
    if (it == payload.end() || !it->second.is_string()) {
        return std::nullopt;
    }
    return it->second.get_string();
}

void log_proxy_ingress(
    const std::string_view event,
    const std::string_view route,
    const HeaderMap& headers,
    const std::size_t body_bytes = 0
) {
    std::ostringstream detail;
    detail << "route=" << route << " header_count=" << headers.size() << " body_bytes=" << body_bytes;
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "proxy_server", event, detail.str());
}

void log_proxy_complete(
    const std::string_view event,
    const std::string_view route,
    const int status,
    const std::size_t body_bytes = 0,
    const std::size_t event_count = 0
) {
    std::ostringstream detail;
    detail << "route=" << route << " status=" << status << " body_bytes=" << body_bytes << " event_count=" << event_count;
    core::logging::log_event(core::logging::LogLevel::Info, "runtime", "proxy_server", event, detail.str());
}

using ResponsesWs = uWS::WebSocket<false, true, WsRequestContext>;

struct WsConnectionHandle {
    std::mutex mutex;
    ResponsesWs* ws = nullptr;
};

std::mutex& ws_connection_registry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, std::shared_ptr<WsConnectionHandle>>& ws_connection_registry() {
    static std::unordered_map<std::string, std::shared_ptr<WsConnectionHandle>> registry;
    return registry;
}

std::string next_ws_connection_id() {
    static std::atomic<std::uint64_t> counter{0};
    return "ws_" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed) + 1);
}

void register_ws_connection(ResponsesWs* ws) {
    if (ws == nullptr) {
        return;
    }
    auto* context = ws->getUserData();
    if (context == nullptr) {
        return;
    }
    if (context->connection_id.empty()) {
        context->connection_id = next_ws_connection_id();
    }
    std::lock_guard<std::mutex> lock(ws_connection_registry_mutex());
    auto& registry = ws_connection_registry();
    auto& handle = registry[context->connection_id];
    if (handle == nullptr) {
        handle = std::make_shared<WsConnectionHandle>();
    }
    std::lock_guard<std::mutex> handle_lock(handle->mutex);
    handle->ws = ws;
}

void unregister_ws_connection(ResponsesWs* ws) {
    if (ws == nullptr) {
        return;
    }
    auto* context = ws->getUserData();
    if (context == nullptr || context->connection_id.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(ws_connection_registry_mutex());
    auto& registry = ws_connection_registry();
    const auto it = registry.find(context->connection_id);
    if (it == registry.end()) {
        return;
    }
    std::lock_guard<std::mutex> handle_lock(it->second->mutex);
    if (it->second->ws == ws) {
        it->second->ws = nullptr;
    }
    if (it->second.use_count() <= 1 && it->second->ws == nullptr) {
        registry.erase(it);
    }
}

std::shared_ptr<WsConnectionHandle> find_ws_connection(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(ws_connection_registry_mutex());
    const auto& registry = ws_connection_registry();
    const auto it = registry.find(connection_id);
    if (it == registry.end()) {
        return nullptr;
    }
    return it->second;
}

template <typename Fn>
bool run_proxy_request_async(const std::shared_ptr<std::atomic_bool>& aborted, Fn&& fn) {
    auto* loop = uWS::Loop::get();
    if (loop == nullptr) {
        return false;
    }
    return enqueue_async_task([loop, aborted, fn = std::forward<Fn>(fn)]() mutable { fn(loop, aborted); });
}

proxy::openai::HeaderMap merge_response_headers(
    const proxy::openai::HeaderMap& base_headers,
    const proxy::openai::HeaderMap& appended_headers
) {
    auto merged = base_headers;
    for (const auto& [key, value] : appended_headers) {
        if (key.empty() || merged.contains(key)) {
            continue;
        }
        merged[key] = value;
    }
    return merged;
}

RequestContext request_context(uWS::HttpRequest* req) {
    auto headers = http::request_headers(req);
    auto request_id = middleware::ensure_request_id(headers);
    headers["x-tightrope-request-start-ms"] = current_unix_ms_string();
    return {
        .headers = std::move(headers),
        .request_id = std::move(request_id),
    };
}

void attach_api_key_scope_header(HeaderMap& headers, const middleware::ApiKeyModelPolicy& api_key_policy) {
    if (!api_key_policy.key_id.has_value() || api_key_policy.key_id->empty()) {
        return;
    }
    headers["x-tightrope-api-key-id"] = *api_key_policy.key_id;
}

bool authorize_or_write_denial(
    uWS::HttpResponse<false>* res,
    const std::string_view path,
    const HeaderMap& headers,
    const std::string_view request_id,
    middleware::ApiKeyModelPolicy* api_key_policy = nullptr
) {
    auto db_handle = controllers::open_controller_db();
    const auto key_auth = middleware::validate_proxy_api_key_request(db_handle.db, path, headers);
    if (!key_auth.allow) {
        http::write_json(res, key_auth.status, key_auth.body, request_id);
        return false;
    }

    const auto identity_auth = middleware::validate_codex_usage_identity_request(db_handle.db, path, headers);
    if (!identity_auth.allow) {
        http::write_json(res, identity_auth.status, identity_auth.body, request_id);
        return false;
    }

    if (api_key_policy != nullptr) {
        *api_key_policy = middleware::resolve_api_key_model_policy(db_handle.db, headers);
    }

    return true;
}

std::optional<middleware::DecompressionResult> decode_body_or_write_denial(
    uWS::HttpResponse<false>* res,
    std::string body,
    HeaderMap headers,
    const std::string_view request_id
) {
    auto decoded = middleware::decompress_request_body(std::move(body), std::move(headers));
    if (!decoded.ok) {
        http::write_json(res, decoded.status, decoded.error_body, request_id);
        return std::nullopt;
    }
    return decoded;
}

std::string proxy_disabled_http_response() {
    return proxy::openai::build_error_envelope(
        "proxy_disabled",
        "Proxy server is stopped",
        "service_unavailable"
    );
}

std::string proxy_overloaded_http_response() {
    return proxy::openai::build_error_envelope(
        "proxy_overloaded",
        "Proxy request queue is saturated",
        "service_unavailable"
    );
}

bool ensure_proxy_enabled_http(
    uWS::HttpResponse<false>* res,
    const std::string_view method,
    const std::string_view route,
    const HeaderMap& headers,
    const std::string_view request_id,
    const std::string_view transport = "http"
) {
    if (proxy_runtime::is_enabled()) {
        return true;
    }

    const auto body = proxy_disabled_http_response();
    log_proxy_complete("proxy_disabled", route, 503, body.size());
    persist_proxy_request_log(
        ProxyRequestLogContext{
            .method = method,
            .route = route,
            .status_code = 503,
            .request_body = std::string_view{},
            .response_body = body,
            .response_events = {},
            .transport = transport,
            .headers = &headers,
        }
    );
    http::write_json(res, 503, body, request_id);
    return false;
}

bool ensure_proxy_enabled_ws_message(
    uWS::WebSocket<false, true, WsRequestContext>* ws,
    const WsRequestContext& context,
    const std::string_view request_body
) {
    if (proxy_runtime::is_enabled()) {
        return true;
    }

    const auto frame = proxy::openai::build_websocket_error_event_json(
        503,
        "proxy_disabled",
        "Proxy server is stopped",
        "service_unavailable"
    );
    log_proxy_complete("proxy_disabled", context.route, 503, request_body.size(), 1);
    persist_proxy_request_log(
        ProxyRequestLogContext{
            .method = "POST",
            .route = context.route,
            .status_code = 503,
            .request_body = request_body,
            .response_body = std::nullopt,
            .response_events = {frame},
            .transport = "websocket",
            .headers = &context.headers,
        }
    );
    ws->send(frame, uWS::OpCode::TEXT);
    ws->end(1013);
    return false;
}

void wire_models_route(uWS::App& app, const std::string& path) {
    app.get(path, [path](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        auto context = request_context(req);
        log_proxy_ingress("models_ingress", path, context.headers);
        if (!ensure_proxy_enabled_http(res, "GET", path, context.headers, context.request_id)) {
            return;
        }
        if (!firewall::allow_request_or_write_denial(res, path, context.headers)) {
            return;
        }
        if (!authorize_or_write_denial(res, path, context.headers, context.request_id, &context.api_key_policy)) {
            return;
        }
        proxy::ModelListPolicy proxy_policy{
            .allowed_models = context.api_key_policy.allowed_models,
            .enforced_model = context.api_key_policy.enforced_model,
        };
        const auto response = controllers::get_proxy_models(path, proxy_policy, context.headers);
        log_proxy_complete("models_complete", path, response.status, response.body.size());
        persist_proxy_request_log(
            ProxyRequestLogContext{
                .method = "GET",
                .route = path,
                .status_code = response.status,
                .request_body = std::string_view{},
                .response_body = response.body,
                .response_events = {},
                .transport = "http",
                .headers = &context.headers,
                .response_headers = &response.headers,
            }
        );
        http::write_http(res, response.status, response.content_type, response.body, context.request_id, &response.headers);
    });
}

void wire_json_or_sse_route(uWS::App& app, const std::string& path) {
    app.post(path, [path](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        auto context = request_context(req);
        log_proxy_ingress("responses_http_ingress", path, context.headers);
        if (!ensure_proxy_enabled_http(res, "POST", path, context.headers, context.request_id)) {
            return;
        }
        if (!firewall::allow_request_or_write_denial(res, path, context.headers)) {
            return;
        }
        if (!authorize_or_write_denial(res, path, context.headers, context.request_id, &context.api_key_policy)) {
            return;
        }
        attach_api_key_scope_header(context.headers, context.api_key_policy);
        const auto downstream_turn_state = proxy::session::ensure_turn_state_header(context.headers);
        const auto wants_sse = http::header_contains(context.headers, "accept", "text/event-stream");
        http::read_request_body_with_abort(
            res,
            [res, path, headers = std::move(context.headers), request_id = std::move(context.request_id),
             api_key_policy = context.api_key_policy,
             downstream_turn_state = std::move(downstream_turn_state),
             wants_sse](std::string body, std::shared_ptr<std::atomic_bool> aborted) mutable {
                proxy::openai::HeaderMap downstream_headers;
                if (!downstream_turn_state.empty()) {
                    downstream_headers["x-codex-turn-state"] = downstream_turn_state;
                }
                auto decoded = decode_body_or_write_denial(res, std::move(body), std::move(headers), request_id);
                if (!decoded.has_value()) {
                    return;
                }

                body = std::move(decoded->body);
                headers = std::move(decoded->headers);
                const auto policy_decision = middleware::enforce_api_key_request_policy(api_key_policy, body);
                if (!policy_decision.allow) {
                    log_proxy_complete("responses_http_policy_denied", path, policy_decision.status, policy_decision.error_body.size());
                    persist_proxy_request_log(
                        ProxyRequestLogContext{
                            .method = "POST",
                            .route = path,
                            .status_code = policy_decision.status,
                            .request_body = body,
                            .response_body = policy_decision.error_body,
                            .response_events = {},
                            .transport = "http",
                            .headers = &headers,
                        }
                    );
                    http::write_json(
                        res,
                        policy_decision.status,
                        policy_decision.error_body,
                        request_id,
                        &downstream_headers
                    );
                    return;
                }

                auto overload_request_id = request_id;
                auto overload_downstream_headers = downstream_headers;
                auto overload_request_headers = headers;
                auto overload_request_body = body;

                const bool enqueued = run_proxy_request_async(
                    aborted,
                    [res,
                     path = std::string(path),
                     headers = std::move(headers),
                     request_id = std::move(request_id),
                     api_key_policy,
                     downstream_headers = std::move(downstream_headers),
                     wants_sse,
                     body = std::move(body)](uWS::Loop* loop, const std::shared_ptr<std::atomic_bool>& aborted_flag) mutable {
                        auto reservation_db = controllers::open_controller_db();
                        const auto reservation = reserve_api_key_usage(reservation_db.db, api_key_policy, request_id);

                        if (wants_sse) {
                            const auto sse_response = controllers::post_proxy_responses_sse(path, body, headers);
                            settle_api_key_usage(reservation_db.db, reservation, sse_response.status < 400);
                            log_proxy_complete(
                                "responses_sse_complete",
                                path,
                                sse_response.status,
                                body.size(),
                                sse_response.events.size()
                            );
                            persist_proxy_request_log(
                                ProxyRequestLogContext{
                                    .method = "POST",
                                    .route = path,
                                    .status_code = sse_response.status,
                                    .request_body = body,
                                    .response_body = std::nullopt,
                                    .response_events = sse_response.events,
                                    .transport = "http",
                                    .headers = &headers,
                                    .response_headers = &sse_response.headers,
                                    .sticky = sse_response.sticky_reused,
                                }
                            );
                            auto merged_headers = merge_response_headers(downstream_headers, sse_response.headers);
                            auto events = sse_response.events;
                            auto response_request_id = request_id;
                            const auto status = sse_response.status;
                            loop->defer(
                                [res, aborted_flag, merged_headers = std::move(merged_headers), events = std::move(events),
                                 response_request_id = std::move(response_request_id), status]() mutable {
                                    if (aborted_flag->load(std::memory_order_relaxed)) {
                                        return;
                                    }
                                    http::write_sse(res, status, events, response_request_id, &merged_headers);
                                }
                            );
                            return;
                        }

                        const auto json_response = controllers::post_proxy_responses_json(path, body, headers);
                        settle_api_key_usage(reservation_db.db, reservation, json_response.status < 400);
                        log_proxy_complete("responses_http_complete", path, json_response.status, json_response.body.size());
                        persist_proxy_request_log(
                            ProxyRequestLogContext{
                                .method = "POST",
                                .route = path,
                                .status_code = json_response.status,
                                .request_body = body,
                                .response_body = json_response.body,
                                .response_events = {},
                                .transport = "http",
                                .headers = &headers,
                                .response_headers = &json_response.headers,
                                .sticky = json_response.sticky_reused,
                            }
                        );
                        auto merged_headers = merge_response_headers(downstream_headers, json_response.headers);
                        auto response_body = json_response.body;
                        auto response_content_type = json_response.content_type;
                        auto response_request_id = request_id;
                        const auto status = json_response.status;
                        loop->defer(
                            [res, aborted_flag, merged_headers = std::move(merged_headers), response_body = std::move(response_body),
                             response_content_type = std::move(response_content_type),
                             response_request_id = std::move(response_request_id), status]() mutable {
                                if (aborted_flag->load(std::memory_order_relaxed)) {
                                    return;
                                }
                                http::write_http(
                                    res,
                                    status,
                                    response_content_type,
                                    response_body,
                                    response_request_id,
                                    &merged_headers
                                );
                            }
                        );
                    }
                );
                if (!enqueued) {
                    const auto busy_body = proxy_overloaded_http_response();
                    log_proxy_complete("responses_http_queue_overloaded", path, 503, busy_body.size());
                    persist_proxy_request_log(
                        ProxyRequestLogContext{
                            .method = "POST",
                            .route = path,
                            .status_code = 503,
                            .request_body = overload_request_body,
                            .response_body = busy_body,
                            .response_events = {},
                            .transport = "http",
                            .headers = &overload_request_headers,
                        }
                    );
                    http::write_json(res, 503, busy_body, overload_request_id, &overload_downstream_headers);
                    return;
                }
            }
        );
    });
}

void wire_compact_route(uWS::App& app, const std::string& path) {
    app.post(path, [path](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        auto context = request_context(req);
        log_proxy_ingress("compact_ingress", path, context.headers);
        if (!ensure_proxy_enabled_http(res, "POST", path, context.headers, context.request_id)) {
            return;
        }
        if (!firewall::allow_request_or_write_denial(res, path, context.headers)) {
            return;
        }
        if (!authorize_or_write_denial(res, path, context.headers, context.request_id, &context.api_key_policy)) {
            return;
        }
        http::read_request_body(
            res,
            [res, path, headers = std::move(context.headers), request_id = std::move(context.request_id),
             api_key_policy = context.api_key_policy](
                std::string body
            ) mutable {
                auto decoded = decode_body_or_write_denial(res, std::move(body), std::move(headers), request_id);
                if (!decoded.has_value()) {
                    return;
                }

                body = std::move(decoded->body);
                headers = std::move(decoded->headers);
                const auto policy_decision = middleware::enforce_api_key_request_policy(api_key_policy, body);
                if (!policy_decision.allow) {
                    log_proxy_complete("compact_policy_denied", path, policy_decision.status, policy_decision.error_body.size());
                    persist_proxy_request_log(
                        ProxyRequestLogContext{
                            .method = "POST",
                            .route = path,
                            .status_code = policy_decision.status,
                            .request_body = body,
                            .response_body = policy_decision.error_body,
                            .response_events = {},
                            .transport = "http",
                            .headers = &headers,
                        }
                    );
                    http::write_json(res, policy_decision.status, policy_decision.error_body, request_id);
                    return;
                }
                auto reservation_db = controllers::open_controller_db();
                const auto reservation = reserve_api_key_usage(reservation_db.db, api_key_policy, request_id);

                const auto response = controllers::post_proxy_responses_compact(path, body, headers);
                settle_api_key_usage(reservation_db.db, reservation, response.status < 400);
                log_proxy_complete("compact_complete", path, response.status, response.body.size());
                persist_proxy_request_log(
                    ProxyRequestLogContext{
                        .method = "POST",
                        .route = path,
                        .status_code = response.status,
                        .request_body = body,
                        .response_body = response.body,
                        .response_events = {},
                        .transport = "http",
                        .headers = &headers,
                        .response_headers = &response.headers,
                    }
                );
                http::write_http(res, response.status, response.content_type, response.body, request_id, &response.headers);
            }
        );
    });
}

void wire_memories_trace_summarize_route(uWS::App& app, const std::string& path) {
    app.post(path, [path](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        auto context = request_context(req);
        log_proxy_ingress("memories_trace_summarize_ingress", path, context.headers);
        if (!ensure_proxy_enabled_http(res, "POST", path, context.headers, context.request_id)) {
            return;
        }
        if (!firewall::allow_request_or_write_denial(res, path, context.headers)) {
            return;
        }
        if (!authorize_or_write_denial(res, path, context.headers, context.request_id, &context.api_key_policy)) {
            return;
        }
        http::read_request_body(
            res,
            [res, path, headers = std::move(context.headers), request_id = std::move(context.request_id),
             api_key_policy = context.api_key_policy](
                std::string body
            ) mutable {
                auto decoded = decode_body_or_write_denial(res, std::move(body), std::move(headers), request_id);
                if (!decoded.has_value()) {
                    return;
                }

                body = std::move(decoded->body);
                headers = std::move(decoded->headers);
                auto reservation_db = controllers::open_controller_db();
                const auto reservation = reserve_api_key_usage(reservation_db.db, api_key_policy, request_id);

                const auto response = controllers::post_proxy_memories_trace_summarize(path, body, headers);
                settle_api_key_usage(reservation_db.db, reservation, response.status < 400);
                log_proxy_complete("memories_trace_summarize_complete", path, response.status, response.body.size());
                persist_proxy_request_log(
                    ProxyRequestLogContext{
                        .method = "POST",
                        .route = path,
                        .status_code = response.status,
                        .request_body = body,
                        .response_body = response.body,
                        .response_events = {},
                        .transport = "http",
                        .headers = &headers,
                        .response_headers = &response.headers,
                    }
                );
                http::write_http(res, response.status, response.content_type, response.body, request_id, &response.headers);
            }
        );
    });
}

void wire_transcribe_route(uWS::App& app, const std::string& path) {
    app.post(path, [path](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        auto context = request_context(req);
        log_proxy_ingress("transcribe_ingress", path, context.headers);
        if (!ensure_proxy_enabled_http(res, "POST", path, context.headers, context.request_id)) {
            return;
        }
        if (!firewall::allow_request_or_write_denial(res, path, context.headers)) {
            return;
        }
        if (!authorize_or_write_denial(res, path, context.headers, context.request_id, &context.api_key_policy)) {
            return;
        }
        auto model = http::maybe_string(req->getQuery("model"));
        auto prompt = http::maybe_string(req->getQuery("prompt"));
        auto audio_bytes = http::maybe_string(req->getQuery("audio_bytes"));

        http::read_request_body(
            res,
            [res, path, headers = std::move(context.headers), request_id = std::move(context.request_id),
             api_key_policy = context.api_key_policy,
             model = std::move(model), prompt = std::move(prompt), audio_bytes = std::move(audio_bytes)](
                std::string body
            ) mutable {
                auto decoded = decode_body_or_write_denial(res, std::move(body), std::move(headers), request_id);
                if (!decoded.has_value()) {
                    return;
                }

                body = std::move(decoded->body);
                headers = std::move(decoded->headers);

                if (!body.empty()) {
                    Json payload;
                    if (const auto ec = glz::read_json(payload, body); !ec && payload.is_object()) {
                        const auto& object = payload.get_object();
                        if (!model.has_value()) {
                            model = json_string(object, "model");
                        }
                        if (!prompt.has_value()) {
                            prompt = json_string(object, "prompt");
                        }
                        if (!audio_bytes.has_value()) {
                            audio_bytes = json_string(object, "audio_bytes");
                        }
                        if (!audio_bytes.has_value()) {
                            audio_bytes = json_string(object, "audio");
                        }
                    }
                }

                const std::string transcribe_model(proxy::internal::kTranscriptionModel);
                if (!api_key_policy.allowed_models.empty() &&
                    std::find(api_key_policy.allowed_models.begin(), api_key_policy.allowed_models.end(), transcribe_model) ==
                        api_key_policy.allowed_models.end()) {
                    const auto error_body = proxy::openai::build_error_envelope(
                        "model_not_allowed",
                        "This API key does not have access to model '" + transcribe_model + "'",
                        "permission_error"
                    );
                    log_proxy_complete("transcribe_policy_denied", path, 403, error_body.size());
                    persist_proxy_request_log(
                        ProxyRequestLogContext{
                            .method = "POST",
                            .route = path,
                            .status_code = 403,
                            .request_body = body,
                            .response_body = error_body,
                            .response_events = {},
                            .transport = "http",
                            .headers = &headers,
                        }
                    );
                    http::write_json(res, 403, error_body, request_id);
                    return;
                }
                auto reservation_db = controllers::open_controller_db();
                const auto reservation =
                    reserve_api_key_usage(reservation_db.db, api_key_policy, request_id);
                const auto response = controllers::post_proxy_transcribe(
                    path,
                    model.value_or(""),
                    prompt.value_or(""),
                    audio_bytes.value_or(""),
                    headers
                );
                settle_api_key_usage(reservation_db.db, reservation, response.status < 400);
                log_proxy_complete("transcribe_complete", path, response.status, response.body.size());
                persist_proxy_request_log(
                    ProxyRequestLogContext{
                        .method = "POST",
                        .route = path,
                        .status_code = response.status,
                        .request_body = body,
                        .response_body = response.body,
                        .response_events = {},
                        .transport = "http",
                        .headers = &headers,
                        .response_headers = &response.headers,
                    }
                );
                http::write_http(res, response.status, response.content_type, response.body, request_id, &response.headers);
            }
        );
    });
}

void wire_chat_completions_route(uWS::App& app, const std::string& path) {
    app.post(path, [path](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        auto context = request_context(req);
        log_proxy_ingress("chat_completions_ingress", path, context.headers);
        if (!ensure_proxy_enabled_http(res, "POST", path, context.headers, context.request_id)) {
            return;
        }
        if (!firewall::allow_request_or_write_denial(res, path, context.headers)) {
            return;
        }
        if (!authorize_or_write_denial(res, path, context.headers, context.request_id, &context.api_key_policy)) {
            return;
        }

        http::read_request_body(
            res,
            [res, path, headers = std::move(context.headers), request_id = std::move(context.request_id),
             api_key_policy = context.api_key_policy](
                std::string body
            ) mutable {
                auto decoded = decode_body_or_write_denial(res, std::move(body), std::move(headers), request_id);
                if (!decoded.has_value()) {
                    return;
                }

                body = std::move(decoded->body);
                headers = std::move(decoded->headers);
                const auto policy_decision = middleware::enforce_api_key_request_policy(api_key_policy, body);
                if (!policy_decision.allow) {
                    log_proxy_complete(
                        "chat_completions_policy_denied",
                        path,
                        policy_decision.status,
                        policy_decision.error_body.size()
                    );
                    persist_proxy_request_log(
                        ProxyRequestLogContext{
                            .method = "POST",
                            .route = path,
                            .status_code = policy_decision.status,
                            .request_body = body,
                            .response_body = policy_decision.error_body,
                            .response_events = {},
                            .transport = "http",
                            .headers = &headers,
                        }
                    );
                    http::write_json(res, policy_decision.status, policy_decision.error_body, request_id);
                    return;
                }
                auto reservation_db = controllers::open_controller_db();
                const auto reservation = reserve_api_key_usage(reservation_db.db, api_key_policy, request_id);

                if (controllers::proxy_chat_completions_stream_requested(body)) {
                    const auto response = controllers::post_proxy_chat_completions_sse(path, body, headers);
                    settle_api_key_usage(reservation_db.db, reservation, response.status < 400);
                    log_proxy_complete(
                        "chat_completions_sse_complete",
                        path,
                        response.status,
                        body.size(),
                        response.events.size()
                    );
                    persist_proxy_request_log(
                        ProxyRequestLogContext{
                            .method = "POST",
                            .route = path,
                            .status_code = response.status,
                            .request_body = body,
                            .response_body = std::nullopt,
                            .response_events = response.events,
                            .transport = "http",
                            .headers = &headers,
                            .response_headers = &response.headers,
                        }
                    );
                    http::write_sse(res, response.status, response.events, request_id, &response.headers);
                    return;
                }

                const auto response = controllers::post_proxy_chat_completions_json(path, body, headers);
                settle_api_key_usage(reservation_db.db, reservation, response.status < 400);
                log_proxy_complete("chat_completions_json_complete", path, response.status, response.body.size());
                persist_proxy_request_log(
                    ProxyRequestLogContext{
                        .method = "POST",
                        .route = path,
                        .status_code = response.status,
                        .request_body = body,
                        .response_body = response.body,
                        .response_events = {},
                        .transport = "http",
                        .headers = &headers,
                        .response_headers = &response.headers,
                    }
                );
                http::write_http(res, response.status, response.content_type, response.body, request_id, &response.headers);
            }
        );
    });
}

void wire_codex_usage_route(uWS::App& app, const std::string& path) {
    app.get(path, [path](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        auto context = request_context(req);
        log_proxy_ingress("codex_usage_ingress", path, context.headers);
        if (!ensure_proxy_enabled_http(res, "GET", path, context.headers, context.request_id)) {
            return;
        }

        auto db_handle = controllers::open_controller_db();
        const auto identity_auth =
            middleware::validate_codex_usage_identity_request(db_handle.db, path, context.headers);
        if (!identity_auth.allow) {
            http::write_json(res, identity_auth.status, identity_auth.body, context.request_id);
            return;
        }

        const auto response = controllers::get_codex_usage(context.headers, db_handle.db);
        log_proxy_complete("codex_usage_complete", path, response.status, response.body.size());
        http::write_json(res, response.status, response.body, context.request_id);
    });
}

void wire_ws_route(uWS::App& app, const std::string& path) {
    uWS::App::WebSocketBehavior<WsRequestContext> behavior{};
    behavior.idleTimeout = 30;
    behavior.maxPayloadLength = kResponsesWsMaxPayloadBytes;
    behavior.compression = uWS::DISABLED;
    behavior.upgrade = [path](uWS::HttpResponse<false>* res, uWS::HttpRequest* req, us_socket_context_t* context) {
        auto request = request_context(req);
        log_proxy_ingress("responses_ws_upgrade_ingress", path, request.headers);
        if (!ensure_proxy_enabled_http(res, "POST", path, request.headers, request.request_id, "websocket")) {
            return;
        }
        if (!firewall::allow_request_or_write_denial(res, path, request.headers)) {
            return;
        }
        if (!authorize_or_write_denial(res, path, request.headers, request.request_id, &request.api_key_policy)) {
            return;
        }
        attach_api_key_scope_header(request.headers, request.api_key_policy);
        const auto downstream_turn_state = proxy::session::ensure_turn_state_header(request.headers);

        const auto sec_websocket_key = req->getHeader("sec-websocket-key");
        if (sec_websocket_key.data() == nullptr || sec_websocket_key.empty()) {
            http::write_http(
                res,
                400,
                "application/json",
                R"({"error":{"message":"Missing websocket key"}})",
                request.request_id
            );
            return;
        }

        res->writeStatus("101 Switching Protocols");
        if (!downstream_turn_state.empty()) {
            res->writeHeader("x-codex-turn-state", downstream_turn_state);
        }
        res->template upgrade<WsRequestContext>(
            WsRequestContext{
                .route = path,
                .headers = std::move(request.headers),
                .request_id = std::move(request.request_id),
                .api_key_policy = std::move(request.api_key_policy),
                .connection_id = {},
                .message_mutex = std::make_shared<std::mutex>(),
            },
            sec_websocket_key,
            req->getHeader("sec-websocket-protocol"),
            req->getHeader("sec-websocket-extensions"),
            context
        );
    };
    behavior.open = [](ResponsesWs* ws) { register_ws_connection(ws); };
    behavior.close = [](ResponsesWs* ws, int, std::string_view) {
        if (auto* context = ws->getUserData(); context != nullptr) {
            proxy::request_upstream_websocket_bridge_cancel(context->headers);
        }
        unregister_ws_connection(ws);
    };
    behavior.message = [](ResponsesWs* ws, std::string_view message, uWS::OpCode op) {
        if (op != uWS::OpCode::TEXT && op != uWS::OpCode::BINARY) {
            return;
        }
        auto* context = ws->getUserData();
        if (context == nullptr) {
            return;
        }
        if (context->message_mutex == nullptr) {
            context->message_mutex = std::make_shared<std::mutex>();
        }
        context->headers["x-tightrope-request-start-ms"] = current_unix_ms_string();
        log_proxy_ingress("responses_ws_message_ingress", context->route, context->headers, message.size());
        auto request_body = http::to_string_safe(message);
        if (!ensure_proxy_enabled_ws_message(ws, *context, request_body)) {
            return;
        }
        const auto message_request_id =
            context->request_id + ":" + std::to_string(++context->message_sequence);
        const auto policy_decision = middleware::enforce_api_key_request_policy(context->api_key_policy, request_body);
        if (!policy_decision.allow) {
            const auto code = policy_decision.error_code.empty() ? std::string("model_not_allowed") : policy_decision.error_code;
            const auto message_text = policy_decision.error_message.empty()
                                          ? std::string("This API key does not have access to the requested model")
                                          : policy_decision.error_message;
            const auto error_type = policy_decision.error_type.empty() ? std::string("permission_error")
                                                                       : policy_decision.error_type;
            const auto error_frame =
                proxy::openai::build_websocket_error_event_json(policy_decision.status, code, message_text, error_type);
            log_proxy_complete("responses_ws_policy_denied", context->route, policy_decision.status, 0, 1);
            persist_proxy_request_log(
                ProxyRequestLogContext{
                    .method = "POST",
                    .route = context->route,
                    .status_code = policy_decision.status,
                    .request_body = request_body,
                    .response_body = std::nullopt,
                    .response_events = {error_frame},
                    .transport = "websocket",
                    .headers = &context->headers,
                }
            );
            ws->send(error_frame, uWS::OpCode::TEXT);
            ws->end(1008);
            return;
        }
        const auto connection_id = context->connection_id;
        auto route = context->route;
        auto headers = context->headers;
        auto api_key_policy = context->api_key_policy;
        const auto binary_payload = op == uWS::OpCode::BINARY;
        auto message_mutex = context->message_mutex;

        const bool enqueued = run_proxy_request_async(
            std::make_shared<std::atomic_bool>(false),
            [connection_id = std::move(connection_id),
             route = std::move(route),
             headers = std::move(headers),
             api_key_policy = std::move(api_key_policy),
             message_request_id = std::move(message_request_id),
             request_body = std::move(request_body),
             binary_payload,
             message_mutex = std::move(message_mutex)](
                uWS::Loop* loop,
                const std::shared_ptr<std::atomic_bool>&
            ) mutable {
                if (loop == nullptr) {
                    return;
                }
                const auto process_message = [&]() {
                    auto reservation_db = controllers::open_controller_db();
                    const auto reservation =
                        reserve_api_key_usage(reservation_db.db, api_key_policy, message_request_id);

                    const auto response =
                        controllers::proxy_responses_websocket(route, request_body, headers, binary_payload);
                    settle_api_key_usage(
                        reservation_db.db,
                        reservation,
                        response.status < 400 && response.accepted && response.close_code == 1000
                    );
                    log_proxy_complete("responses_ws_message_complete", route, response.status, 0, response.frames.size());
                    persist_proxy_request_log(
                        ProxyRequestLogContext{
                            .method = "POST",
                            .route = route,
                            .status_code = response.status,
                            .request_body = request_body,
                            .response_body = std::nullopt,
                            .response_events = response.frames,
                            .transport = "websocket",
                            .headers = &headers,
                            .routed_account_id = response.routed_account_id,
                            .sticky = response.sticky_reused,
                        }
                    );

                    auto frames = response.frames;
                    const auto close_code = response.close_code;
                    const auto accepted = response.accepted;
                    loop->defer(
                        [connection_id, frames = std::move(frames), close_code, accepted]() mutable {
                            auto connection_handle = find_ws_connection(connection_id);
                            if (connection_handle == nullptr) {
                                return;
                            }
                            std::lock_guard<std::mutex> handle_lock(connection_handle->mutex);
                            auto* active_ws = connection_handle->ws;
                            if (active_ws == nullptr) {
                                return;
                            }
                            for (const auto& frame : frames) {
                                active_ws->send(frame, uWS::OpCode::TEXT);
                            }
                            if (close_code != 1000 || !accepted) {
                                active_ws->end(close_code);
                            }
                        }
                    );
                };

                if (message_mutex) {
                    std::lock_guard<std::mutex> lock(*message_mutex);
                    process_message();
                    return;
                }
                process_message();
            }
        );
        if (!enqueued) {
            const auto frame = proxy::openai::build_websocket_error_event_json(
                503,
                "proxy_overloaded",
                "Proxy request queue is saturated",
                "service_unavailable"
            );
            log_proxy_complete("responses_ws_queue_overloaded", context->route, 503, request_body.size(), 1);
            persist_proxy_request_log(
                ProxyRequestLogContext{
                    .method = "POST",
                    .route = context->route,
                    .status_code = 503,
                    .request_body = request_body,
                    .response_body = std::nullopt,
                    .response_events = {frame},
                    .transport = "websocket",
                    .headers = &context->headers,
                }
            );
            ws->send(frame, uWS::OpCode::TEXT);
            ws->end(1013);
            return;
        }
    };
    app.ws<WsRequestContext>(path, std::move(behavior));
}

} // namespace

void wire_routes(uWS::App& app, Runtime* runtime) {
    app.get("/health", [runtime](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        const auto health = controllers::get_health(*runtime);
        const auto body =
            std::string(R"({"status":")") + health.status + R"(","uptime_ms":)" + std::to_string(health.uptime_ms) +
            "}";
        http::write_json(res, 200, body);
    });

    wire_models_route(app, "/api/models");
    wire_models_route(app, "/v1/models");
    wire_models_route(app, "/backend-api/codex/models");

    wire_json_or_sse_route(app, "/v1/responses");
    wire_json_or_sse_route(app, "/backend-api/codex/responses");

    wire_compact_route(app, "/v1/responses/compact");
    wire_compact_route(app, "/backend-api/codex/responses/compact");

    wire_memories_trace_summarize_route(app, "/v1/memories/trace_summarize");
    wire_memories_trace_summarize_route(app, "/backend-api/codex/memories/trace_summarize");

    wire_transcribe_route(app, "/backend-api/transcribe");
    wire_transcribe_route(app, "/v1/audio/transcriptions");

    wire_chat_completions_route(app, "/v1/chat/completions");

    wire_codex_usage_route(app, "/api/codex/usage");
    wire_codex_usage_route(app, "/api/codex/usage/");

    wire_ws_route(app, "/v1/responses");
    wire_ws_route(app, "/backend-api/codex/responses");

    firewall::wire_routes(app);
    admin::wire_routes(app);
}

} // namespace tightrope::server::internal
