#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace tightrope::server::controllers {

struct LinearizableReadGuardResult {
    bool allow = true;
    int status = 200;
    std::string code;
    std::string message;
};

LinearizableReadGuardResult check_linearizable_read_access(std::string_view table_name) noexcept;
void set_linearizable_read_override_for_testing(std::optional<bool> allow, std::optional<std::string> leader_id = std::nullopt);
void clear_linearizable_read_override_for_testing();

} // namespace tightrope::server::controllers
