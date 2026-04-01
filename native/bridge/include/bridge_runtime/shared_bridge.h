#pragma once

#include <mutex>

#include "bridge.h"

namespace tightrope::bridge::runtime {

Bridge& shared_bridge_instance();
std::mutex& shared_bridge_mutex();

} // namespace tightrope::bridge::runtime
