#pragma once
#include "common/racing/Routes.h"

namespace coop::street {
// Old route previews and event/checkpoint blips were plain coordinate blips
// with these colours, at exact catalog coordinates. Keep unrelated radar icons.
inline bool LegacyRaceBlip(bool used, uint8_t type, uint8_t sprite, uint32_t colour, Vec3 position) {
    if (!used || type != 4 || sprite != 0 || (colour != 0 && colour != 1 && colour != 3)) return false;
    for (const auto& route : Routes())
        for (const auto& point : route.checkpoints)
            if (Distance(position, point) < 0.25f) return true;
    return false;
}
}
