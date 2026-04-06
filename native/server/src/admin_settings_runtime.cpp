#include "internal/admin_runtime_parts.h"

#include <optional>
#include <string>
#include <vector>

#include "internal/admin_runtime_common.h"
#include "internal/http_runtime_utils.h"
#include "text/json_escape.h"
#include "controllers/settings_controller.h"

namespace tightrope::server::internal::admin {

namespace {

std::string string_array_json(const std::vector<std::string>& values) {
    std::string json = "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            json.push_back(',');
        }
        json += core::text::quote_json_string(values[index]);
    }
    json.push_back(']');
    return json;
}

std::string settings_json(const controllers::DashboardSettingsPayload& settings) {
    return std::string(R"({"theme":)") + core::text::quote_json_string(settings.theme) +
           R"(,"stickyThreadsEnabled":)" + bool_json(settings.sticky_threads_enabled) +
           R"(,"upstreamStreamTransport":)" + core::text::quote_json_string(settings.upstream_stream_transport) +
           R"(,"preferEarlierResetAccounts":)" + bool_json(settings.prefer_earlier_reset_accounts) +
           R"(,"routingStrategy":)" + core::text::quote_json_string(settings.routing_strategy) +
           R"(,"strictLockPoolContinuations":)" + bool_json(settings.strict_lock_pool_continuations) +
           R"(,"lockedRoutingAccountIds":)" + string_array_json(settings.locked_routing_account_ids) +
           R"(,"openaiCacheAffinityMaxAgeSeconds":)" +
           std::to_string(settings.openai_cache_affinity_max_age_seconds) + R"(,"importWithoutOverwrite":)" +
           bool_json(settings.import_without_overwrite) + R"(,"totpRequiredOnLogin":)" +
           bool_json(settings.totp_required_on_login) + R"(,"totpConfigured":)" + bool_json(settings.totp_configured) +
           R"(,"apiKeyAuthEnabled":)" + bool_json(settings.api_key_auth_enabled) +
           R"(,"routingHeadroomWeightPrimary":)" + std::to_string(settings.routing_headroom_weight_primary) +
           R"(,"routingHeadroomWeightSecondary":)" + std::to_string(settings.routing_headroom_weight_secondary) +
           R"(,"routingScoreAlpha":)" + std::to_string(settings.routing_score_alpha) + R"(,"routingScoreBeta":)" +
           std::to_string(settings.routing_score_beta) + R"(,"routingScoreGamma":)" +
           std::to_string(settings.routing_score_gamma) + R"(,"routingScoreDelta":)" +
           std::to_string(settings.routing_score_delta) + R"(,"routingScoreZeta":)" +
           std::to_string(settings.routing_score_zeta) + R"(,"routingScoreEta":)" +
           std::to_string(settings.routing_score_eta) + R"(,"routingSuccessRateRho":)" +
           std::to_string(settings.routing_success_rate_rho) +
           R"(,"routingPlanModelPricingUsdPerMillion":)" +
           core::text::quote_json_string(settings.routing_plan_model_pricing_usd_per_million) +
           R"(,"syncClusterName":)" +
           core::text::quote_json_string(settings.sync_cluster_name) + R"(,"syncSiteId":)" +
           std::to_string(settings.sync_site_id) + R"(,"syncPort":)" + std::to_string(settings.sync_port) +
           R"(,"syncDiscoveryEnabled":)" + bool_json(settings.sync_discovery_enabled) +
           R"(,"syncIntervalSeconds":)" + std::to_string(settings.sync_interval_seconds) +
           R"(,"syncConflictResolution":)" + core::text::quote_json_string(settings.sync_conflict_resolution) +
           R"(,"syncJournalRetentionDays":)" + std::to_string(settings.sync_journal_retention_days) +
           R"(,"syncTlsEnabled":)" + bool_json(settings.sync_tls_enabled) +
           R"(,"syncRequireHandshakeAuth":)" + bool_json(settings.sync_require_handshake_auth) +
           R"(,"syncClusterSharedSecret":)" + core::text::quote_json_string(settings.sync_cluster_shared_secret) +
           R"(,"syncTlsVerifyPeer":)" + bool_json(settings.sync_tls_verify_peer) +
           R"(,"syncTlsCaCertificatePath":)" + core::text::quote_json_string(settings.sync_tls_ca_certificate_path) +
           R"(,"syncTlsCertificateChainPath":)" +
           core::text::quote_json_string(settings.sync_tls_certificate_chain_path) +
           R"(,"syncTlsPrivateKeyPath":)" + core::text::quote_json_string(settings.sync_tls_private_key_path) +
           R"(,"syncTlsPinnedPeerCertificateSha256":)" +
           core::text::quote_json_string(settings.sync_tls_pinned_peer_certificate_sha256) +
           R"(,"syncSchemaVersion":)" + std::to_string(settings.sync_schema_version) +
           R"(,"syncMinSupportedSchemaVersion":)" + std::to_string(settings.sync_min_supported_schema_version) +
           R"(,"syncAllowSchemaDowngrade":)" + bool_json(settings.sync_allow_schema_downgrade) +
           R"(,"syncPeerProbeEnabled":)" + bool_json(settings.sync_peer_probe_enabled) +
           R"(,"syncPeerProbeIntervalMs":)" + std::to_string(settings.sync_peer_probe_interval_ms) +
           R"(,"syncPeerProbeTimeoutMs":)" + std::to_string(settings.sync_peer_probe_timeout_ms) +
           R"(,"syncPeerProbeMaxPerRefresh":)" + std::to_string(settings.sync_peer_probe_max_per_refresh) +
           R"(,"syncPeerProbeFailClosed":)" + bool_json(settings.sync_peer_probe_fail_closed) +
           R"(,"syncPeerProbeFailClosedFailures":)" + std::to_string(settings.sync_peer_probe_fail_closed_failures) +
           "}";
}

} // namespace

void wire_settings_routes(uWS::App& app) {
    app.get("/api/settings", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        const auto response = controllers::get_settings();
        if (response.status == 200) {
            http::write_json(res, 200, settings_json(response.settings));
            return;
        }
        http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
    });

    app.put("/api/settings", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        http::read_request_body(res, [res](std::string body) mutable {
            const auto parsed = parse_json_object(body);
            if (!parsed.has_value()) {
                http::write_json(res, 400, dashboard_error_json("invalid_request", "Invalid JSON payload"));
                return;
            }

            controllers::DashboardSettingsUpdate update{};
            update.theme = json_string(*parsed, "theme");
            update.sticky_threads_enabled = json_bool(*parsed, "stickyThreadsEnabled")
                                                .or_else([&] { return json_bool(*parsed, "sticky_threads_enabled"); });
            update.upstream_stream_transport = json_string(*parsed, "upstreamStreamTransport")
                                                   .or_else([&] { return json_string(*parsed, "upstream_stream_transport"); });
            update.prefer_earlier_reset_accounts = json_bool(*parsed, "preferEarlierResetAccounts")
                                                       .or_else([&] {
                                                           return json_bool(*parsed, "prefer_earlier_reset_accounts");
                                                       });
            update.routing_strategy = json_string(*parsed, "routingStrategy")
                                          .or_else([&] { return json_string(*parsed, "routing_strategy"); });
            update.strict_lock_pool_continuations = json_bool(*parsed, "strictLockPoolContinuations")
                                                        .or_else([&] {
                                                            return json_bool(
                                                                *parsed,
                                                                "strict_lock_pool_continuations"
                                                            );
                                                        });
            if (const auto locked_ids = parsed->find("lockedRoutingAccountIds");
                locked_ids != parsed->end() && locked_ids->second.is_array()) {
                update.locked_routing_account_ids = json_string_array(*parsed, "lockedRoutingAccountIds");
            } else if (
                const auto locked_ids = parsed->find("locked_routing_account_ids");
                locked_ids != parsed->end() && locked_ids->second.is_array()) {
                update.locked_routing_account_ids = json_string_array(*parsed, "locked_routing_account_ids");
            }
            update.openai_cache_affinity_max_age_seconds = json_int64(*parsed, "openaiCacheAffinityMaxAgeSeconds")
                                                               .or_else([&] {
                                                                   return json_int64(
                                                                       *parsed,
                                                                       "openai_cache_affinity_max_age_seconds"
                                                                   );
                                                               });
            update.import_without_overwrite = json_bool(*parsed, "importWithoutOverwrite")
                                                  .or_else([&] { return json_bool(*parsed, "import_without_overwrite"); });
            update.totp_required_on_login = json_bool(*parsed, "totpRequiredOnLogin")
                                                .or_else([&] { return json_bool(*parsed, "totp_required_on_login"); });
            update.api_key_auth_enabled = json_bool(*parsed, "apiKeyAuthEnabled")
                                              .or_else([&] { return json_bool(*parsed, "api_key_auth_enabled"); });
            update.routing_headroom_weight_primary = json_double(*parsed, "routingHeadroomWeightPrimary")
                                                         .or_else([&] {
                                                             return json_double(
                                                                 *parsed,
                                                                 "routing_headroom_weight_primary"
                                                             );
                                                         });
            update.routing_headroom_weight_secondary = json_double(*parsed, "routingHeadroomWeightSecondary")
                                                           .or_else([&] {
                                                               return json_double(
                                                                   *parsed,
                                                                   "routing_headroom_weight_secondary"
                                                               );
                                                           });
            update.routing_score_alpha = json_double(*parsed, "routingScoreAlpha")
                                             .or_else([&] { return json_double(*parsed, "routing_score_alpha"); });
            update.routing_score_beta = json_double(*parsed, "routingScoreBeta")
                                            .or_else([&] { return json_double(*parsed, "routing_score_beta"); });
            update.routing_score_gamma = json_double(*parsed, "routingScoreGamma")
                                             .or_else([&] { return json_double(*parsed, "routing_score_gamma"); });
            update.routing_score_delta = json_double(*parsed, "routingScoreDelta")
                                             .or_else([&] { return json_double(*parsed, "routing_score_delta"); });
            update.routing_score_zeta = json_double(*parsed, "routingScoreZeta")
                                            .or_else([&] { return json_double(*parsed, "routing_score_zeta"); });
            update.routing_score_eta = json_double(*parsed, "routingScoreEta")
                                           .or_else([&] { return json_double(*parsed, "routing_score_eta"); });
            update.routing_success_rate_rho = json_double(*parsed, "routingSuccessRateRho")
                                                  .or_else([&] {
                                                      return json_double(
                                                          *parsed,
                                                          "routing_success_rate_rho"
                                                      );
                                                  });
            update.routing_plan_model_pricing_usd_per_million =
                json_string(*parsed, "routingPlanModelPricingUsdPerMillion")
                    .or_else([&] {
                        return json_string(
                            *parsed,
                            "routing_plan_model_pricing_usd_per_million"
                        );
                    });
            update.sync_cluster_name = json_string(*parsed, "syncClusterName")
                                           .or_else([&] { return json_string(*parsed, "sync_cluster_name"); });
            update.sync_site_id = json_int64(*parsed, "syncSiteId")
                                      .or_else([&] { return json_int64(*parsed, "sync_site_id"); });
            update.sync_port = json_int64(*parsed, "syncPort")
                                   .or_else([&] { return json_int64(*parsed, "sync_port"); });
            update.sync_discovery_enabled = json_bool(*parsed, "syncDiscoveryEnabled")
                                                .or_else([&] {
                                                    return json_bool(*parsed, "sync_discovery_enabled");
                                                });
            update.sync_interval_seconds = json_int64(*parsed, "syncIntervalSeconds")
                                               .or_else([&] {
                                                   return json_int64(*parsed, "sync_interval_seconds");
                                               });
            update.sync_conflict_resolution = json_string(*parsed, "syncConflictResolution")
                                                  .or_else([&] {
                                                      return json_string(*parsed, "sync_conflict_resolution");
                                                  });
            update.sync_journal_retention_days = json_int64(*parsed, "syncJournalRetentionDays")
                                                     .or_else([&] {
                                                         return json_int64(
                                                             *parsed,
                                                             "sync_journal_retention_days"
                                                         );
                                                     });
            update.sync_tls_enabled = json_bool(*parsed, "syncTlsEnabled")
                                          .or_else([&] { return json_bool(*parsed, "sync_tls_enabled"); });
            update.sync_require_handshake_auth = json_bool(*parsed, "syncRequireHandshakeAuth")
                                                     .or_else([&] {
                                                         return json_bool(
                                                             *parsed,
                                                             "sync_require_handshake_auth"
                                                         );
                                                     });
            update.sync_cluster_shared_secret = json_string(*parsed, "syncClusterSharedSecret")
                                                    .or_else([&] {
                                                        return json_string(
                                                            *parsed,
                                                            "sync_cluster_shared_secret"
                                                        );
                                                    });
            update.sync_tls_verify_peer = json_bool(*parsed, "syncTlsVerifyPeer")
                                              .or_else([&] { return json_bool(*parsed, "sync_tls_verify_peer"); });
            update.sync_tls_ca_certificate_path = json_string(*parsed, "syncTlsCaCertificatePath")
                                                      .or_else([&] {
                                                          return json_string(
                                                              *parsed,
                                                              "sync_tls_ca_certificate_path"
                                                          );
                                                      });
            update.sync_tls_certificate_chain_path = json_string(*parsed, "syncTlsCertificateChainPath")
                                                         .or_else([&] {
                                                             return json_string(
                                                                 *parsed,
                                                                 "sync_tls_certificate_chain_path"
                                                             );
                                                         });
            update.sync_tls_private_key_path = json_string(*parsed, "syncTlsPrivateKeyPath")
                                                   .or_else([&] {
                                                       return json_string(
                                                           *parsed,
                                                           "sync_tls_private_key_path"
                                                       );
                                                   });
            update.sync_tls_pinned_peer_certificate_sha256 =
                json_string(*parsed, "syncTlsPinnedPeerCertificateSha256")
                    .or_else([&] {
                        return json_string(
                            *parsed,
                            "sync_tls_pinned_peer_certificate_sha256"
                        );
                    });
            update.sync_schema_version = json_int64(*parsed, "syncSchemaVersion")
                                             .or_else([&] { return json_int64(*parsed, "sync_schema_version"); });
            update.sync_min_supported_schema_version =
                json_int64(*parsed, "syncMinSupportedSchemaVersion")
                    .or_else([&] {
                        return json_int64(
                            *parsed,
                            "sync_min_supported_schema_version"
                        );
                    });
            update.sync_allow_schema_downgrade = json_bool(*parsed, "syncAllowSchemaDowngrade")
                                                     .or_else([&] {
                                                         return json_bool(
                                                             *parsed,
                                                             "sync_allow_schema_downgrade"
                                                         );
                                                     });
            update.sync_peer_probe_enabled = json_bool(*parsed, "syncPeerProbeEnabled")
                                                 .or_else([&] {
                                                     return json_bool(
                                                         *parsed,
                                                         "sync_peer_probe_enabled"
                                                     );
                                                 });
            update.sync_peer_probe_interval_ms = json_int64(*parsed, "syncPeerProbeIntervalMs")
                                                     .or_else([&] {
                                                         return json_int64(
                                                             *parsed,
                                                             "sync_peer_probe_interval_ms"
                                                         );
                                                     });
            update.sync_peer_probe_timeout_ms = json_int64(*parsed, "syncPeerProbeTimeoutMs")
                                                    .or_else([&] {
                                                        return json_int64(
                                                            *parsed,
                                                            "sync_peer_probe_timeout_ms"
                                                        );
                                                    });
            update.sync_peer_probe_max_per_refresh = json_int64(*parsed, "syncPeerProbeMaxPerRefresh")
                                                         .or_else([&] {
                                                             return json_int64(
                                                                 *parsed,
                                                                 "sync_peer_probe_max_per_refresh"
                                                             );
                                                         });
            update.sync_peer_probe_fail_closed = json_bool(*parsed, "syncPeerProbeFailClosed")
                                                     .or_else([&] {
                                                         return json_bool(
                                                             *parsed,
                                                             "sync_peer_probe_fail_closed"
                                                         );
                                                     });
            update.sync_peer_probe_fail_closed_failures =
                json_int64(*parsed, "syncPeerProbeFailClosedFailures")
                    .or_else([&] {
                        return json_int64(
                            *parsed,
                            "sync_peer_probe_fail_closed_failures"
                        );
                    });

            const auto response = controllers::update_settings(update);
            if (response.status == 200) {
                http::write_json(res, 200, settings_json(response.settings));
                return;
            }
            http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
        });
    });

    app.post("/api/settings/database/passphrase", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        static_cast<void>(req);
        http::read_request_body(res, [res](std::string body) mutable {
            const auto parsed = parse_json_object(body);
            if (!parsed.has_value()) {
                http::write_json(res, 400, dashboard_error_json("invalid_request", "Invalid JSON payload"));
                return;
            }

            const auto current_passphrase = json_string(*parsed, "currentPassphrase")
                                                .or_else([&] { return json_string(*parsed, "current_passphrase"); });
            const auto next_passphrase = json_string(*parsed, "nextPassphrase")
                                             .or_else([&] { return json_string(*parsed, "next_passphrase"); })
                                             .or_else([&] { return json_string(*parsed, "newPassphrase"); })
                                             .or_else([&] { return json_string(*parsed, "new_passphrase"); });
            if (!current_passphrase.has_value() || !next_passphrase.has_value()) {
                http::write_json(
                    res,
                    400,
                    dashboard_error_json(
                        "invalid_request",
                        "currentPassphrase and nextPassphrase are required"
                    )
                );
                return;
            }

            const auto response =
                controllers::change_database_passphrase(*current_passphrase, *next_passphrase);
            if (response.status == 200) {
                http::write_json(res, 200, R"({"status":"ok"})");
                return;
            }
            http::write_json(res, response.status, dashboard_error_json(response.code, response.message));
        });
    });

    app.get("/api/settings/runtime/connect-address", [](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
        const auto response = controllers::get_runtime_connect_address(request_host(req));
        http::write_json(
            res,
            response.status,
            std::string(R"({"connectAddress":)") + core::text::quote_json_string(response.connect_address) + "}"
        );
    });
}

} // namespace tightrope::server::internal::admin
