#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "upstream_transport.h"

namespace tightrope::proxy::internal {

inline constexpr int kStreamRetryBudget = 2;
inline constexpr int kCompactRetryBudget = 1;

using RetryPredicate = bool (*)(const UpstreamExecutionResult&);

struct UpstreamErrorDetails {
    std::string code;
    std::string message;
    std::string type;
    std::string param;
};

std::string normalize_upstream_error_code(std::string_view code, std::string_view error_type = {});
std::string resolved_upstream_error_code(const UpstreamExecutionResult& upstream);
std::string extract_error_code_from_stream_events(const std::vector<std::string>& events);
UpstreamErrorDetails extract_upstream_error_details(const UpstreamExecutionResult& upstream);
std::string default_error_message_for_code(std::string_view normalized_error_code);
bool upstream_401_body_indicates_deactivated_account(const UpstreamExecutionResult& upstream);
std::optional<std::string> exhausted_account_status_from_upstream(const UpstreamExecutionResult& upstream);

bool should_retry_stream_result(const UpstreamExecutionResult& upstream);
bool should_retry_stream_result_with_incomplete(const UpstreamExecutionResult& upstream);
bool should_retry_compact_result(const UpstreamExecutionResult& upstream);
UpstreamExecutionResult execute_upstream_with_retry_budget(
    const openai::UpstreamRequestPlan& plan,
    int retry_budget,
    RetryPredicate should_retry,
    std::string_view retry_event
);

} // namespace tightrope::proxy::internal
