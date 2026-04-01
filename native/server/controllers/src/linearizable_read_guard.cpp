#include "linearizable_read_guard.h"

#include <mutex>

#include "bridge_runtime/shared_bridge.h"
#include "conflict_resolver.h"

namespace tightrope::server::controllers {

namespace {

std::mutex& override_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::optional<bool>& override_allow() {
    static std::optional<bool> value;
    return value;
}

std::optional<std::string>& override_leader_id() {
    static std::optional<std::string> value;
    return value;
}

LinearizableReadGuardResult reject_result(std::string_view table_name, const std::optional<std::string>& leader_id) {
    LinearizableReadGuardResult result;
    result.allow = false;
    result.status = 503;
    result.code = "linearizable_read_requires_leader";
    result.message = "Linearizable read for table '" + std::string(table_name) + "' requires cluster leader";
    if (leader_id.has_value() && !leader_id->empty()) {
        result.message += " (leader site_id=" + *leader_id + ")";
    } else {
        result.message += " (leader unavailable)";
    }
    return result;
}

} // namespace

LinearizableReadGuardResult check_linearizable_read_access(const std::string_view table_name) noexcept {
    if (!sync::table_requires_raft(table_name)) {
        return {};
    }

    {
        std::lock_guard<std::mutex> lock(override_mutex());
        if (override_allow().has_value()) {
            if (*override_allow()) {
                return {};
            }
            return reject_result(table_name, override_leader_id());
        }
    }

    std::lock_guard<std::mutex> lock(bridge::runtime::shared_bridge_mutex());
    std::optional<std::string> leader_id;
    if (bridge::runtime::shared_bridge_instance().linearizable_reads_allowed(&leader_id)) {
        return {};
    }
    return reject_result(table_name, leader_id);
}

void set_linearizable_read_override_for_testing(
    const std::optional<bool> allow,
    std::optional<std::string> leader_id
) {
    std::lock_guard<std::mutex> lock(override_mutex());
    override_allow() = allow;
    override_leader_id() = std::move(leader_id);
}

void clear_linearizable_read_override_for_testing() {
    std::lock_guard<std::mutex> lock(override_mutex());
    override_allow().reset();
    override_leader_id().reset();
}

} // namespace tightrope::server::controllers
