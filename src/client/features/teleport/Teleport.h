#pragma once
#include <string>
#include <vector>

namespace coop::teleport {

// Chat and F2 share target selection and teleport rules.
bool Command(const std::string& cmd, const std::vector<std::string>& args);

}  // namespace coop::teleport
