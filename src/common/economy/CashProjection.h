#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include "common/economy/CashLedger.h"

namespace coop::economy {
// Native UI projection only. It cannot authorize credits or change the ledger.
// Applying a server snapshot is never counted as native mission income.
struct CashProjection {
    int32_t native=0;
    int64_t flow=0;
    std::map<uint64_t,int32_t> debits;
    int64_t Difference(int32_t actual) const {return (int64_t)actual-native;}
    void Capture(uint64_t request,int32_t delta){if(delta<0)debits[request]=delta;flow+=delta;}
    void Acknowledge(uint64_t sequence){
        for(auto it=debits.begin();it!=debits.end();)it=it->first<=sequence?debits.erase(it):std::next(it);
    }
    int32_t Project(int32_t confirmed){
        int64_t projected=confirmed;for(const auto& [id,delta]:debits)projected+=delta;
        native=(int32_t)std::clamp<int64_t>(projected,0,kMaximumCash);return native;
    }
};
}
