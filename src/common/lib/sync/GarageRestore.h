#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace coop::sync {

// Each returned stored-car record must correspond to one model the host sent.
// Consuming the entry also prevents duplicate records in one response.
inline bool ConsumeGarageRestoreModel(std::vector<uint16_t>& expected, uint16_t model) {
    if (!model) return false;
    auto it = std::find(expected.begin(), expected.end(), model);
    if (it == expected.end()) return false;
    expected.erase(it);
    return true;
}

}  // namespace coop::sync
