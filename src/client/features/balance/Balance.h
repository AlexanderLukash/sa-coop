#pragma once
#include <cstdint>

namespace coop {
// Sets an exact balance. Only the session host can send a change.
bool SetPlayerBalance(uint8_t target, int32_t amount);
}
