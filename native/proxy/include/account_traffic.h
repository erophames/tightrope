#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tightrope::proxy {

struct AccountTrafficSnapshot {
    std::string account_id;
    std::uint64_t up_bytes = 0;
    std::uint64_t down_bytes = 0;
    std::int64_t last_up_at_ms = 0;
    std::int64_t last_down_at_ms = 0;
};

using AccountTrafficUpdateCallback = std::function<void(const AccountTrafficSnapshot&)>;

class ScopedAccountTrafficContext {
public:
    explicit ScopedAccountTrafficContext(std::string_view account_id);
    ~ScopedAccountTrafficContext();
    ScopedAccountTrafficContext(const ScopedAccountTrafficContext&) = delete;
    ScopedAccountTrafficContext& operator=(const ScopedAccountTrafficContext&) = delete;
    ScopedAccountTrafficContext(ScopedAccountTrafficContext&&) = delete;
    ScopedAccountTrafficContext& operator=(ScopedAccountTrafficContext&&) = delete;

private:
    std::string previous_account_id_;
};

[[nodiscard]] std::string_view current_account_traffic_context_account_id() noexcept;

void record_account_upstream_egress(std::string_view account_id, std::size_t bytes);
void record_account_upstream_ingress(std::string_view account_id, std::size_t bytes);
[[nodiscard]] std::vector<AccountTrafficSnapshot> snapshot_account_traffic();
void clear_account_traffic_for_testing();
void set_account_traffic_update_callback(AccountTrafficUpdateCallback callback);
void clear_account_traffic_update_callback();

} // namespace tightrope::proxy
