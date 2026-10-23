#pragma once

#include <cstdint>
#include <map>

#include "common/lib/sync/StateTiming.h"

namespace coop::sync {
// Reliable removals and unreliable states have no shared ordering. Keep a short
// history so a state in flight cannot recreate an entity after its removal.
// Keys consist of an owner byte in bits 16..23 and a local 16-bit entity ID.
class RemovalHistory {
public:
    explicit RemovalHistory(uint64_t retentionMs = 10000) : retentionMs_(retentionMs) {}
    void Remember(uint32_t key, uint64_t now) {
        Expire(now);
        removed_[key] = now;
    }
    bool Contains(uint32_t key, uint64_t now) const {
        auto it = removed_.find(key);
        return it != removed_.end() && AgeMs(now, it->second) <= retentionMs_;
    }
    void Expire(uint64_t now) {
        for (auto it = removed_.begin(); it != removed_.end();)
            if (AgeMs(now, it->second) > retentionMs_) it = removed_.erase(it);
            else ++it;
    }
    void ForgetOwner(uint8_t owner) {
        for (auto it = removed_.begin(); it != removed_.end();)
            if ((it->first >> 16) == owner) it = removed_.erase(it);
            else ++it;
    }
    void Clear() { removed_.clear(); }
private:
    uint64_t retentionMs_;
    std::map<uint32_t, uint64_t> removed_;
};
}
