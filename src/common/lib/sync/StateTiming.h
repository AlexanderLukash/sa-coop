#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "common/lib/buffer/Buffer.h"

namespace coop::sync {

inline bool PlausiblePosition(Vec3 p) {
    constexpr float kWorldLimit = 10000.0f;
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::fabs(p.x) <= kWorldLimit &&
           std::fabs(p.y) <= kWorldLimit && std::fabs(p.z) <= kWorldLimit;
}

inline bool PlausibleVelocity(Vec3 v, float maxSpeed) {
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || maxSpeed <= 0.0f) return false;
    return v.x * v.x + v.y * v.y + v.z * v.z <= maxSpeed * maxSpeed;
}

// Sender clocks are 32-bit millisecond counters and wrap about every 49 days. Signed subtraction
// gives the correct ordering as long as states are never separated by half that interval.
inline bool IsNewer(uint32_t candidate, uint32_t previous) {
    return static_cast<int32_t>(candidate - previous) >= 0;
}

inline uint64_t AgeMs(uint64_t now, uint64_t received) {
    return now >= received ? now - received : 0;
}

inline float ExtrapolationSeconds(uint64_t ageMs, uint32_t maxMs) {
    return static_cast<float>(std::min<uint64_t>(ageMs, maxMs)) / 1000.0f;
}

// Let a mirror coast through a brief jitter spike, then settle instead of replaying stale input forever.
inline float Freshness(uint64_t ageMs, uint32_t graceMs, uint32_t fadeMs) {
    if (ageMs <= graceMs) return 1.0f;
    if (!fadeMs || ageMs - graceMs >= fadeMs) return 0.0f;
    return 1.0f - static_cast<float>(ageMs - graceMs) / static_cast<float>(fadeMs);
}

}  // namespace coop::sync
