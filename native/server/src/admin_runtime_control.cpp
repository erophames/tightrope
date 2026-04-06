#include "internal/admin_runtime_parts.h"

#include <string>

#include "internal/admin_runtime_common.h"
#include "internal/http_runtime_utils.h"
#include "internal/proxy_runtime_control.h"

namespace tightrope::server::internal::admin {

namespace {

std::string proxy_state_json(const bool enabled) {
    return std::string(R"({"enabled":)") + bool_json(enabled) + "}";
}

} // namespace

void wire_runtime_routes(uWS::App& app) {
    app.get("/api/runtime/proxy", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        http::write_json(res, 200, proxy_state_json(proxy_runtime::is_enabled()));
    });

    app.post("/api/runtime/proxy/start", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        proxy_runtime::set_enabled(true);
        http::write_json(res, 200, proxy_state_json(true));
    });

    app.post("/api/runtime/proxy/stop", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        proxy_runtime::set_enabled(false);
        http::write_json(res, 200, proxy_state_json(false));
    });
}

} // namespace tightrope::server::internal::admin
