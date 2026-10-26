#pragma once
#include "common/racing/RaceProtocol.h"

namespace coop {
const street::Snapshot* EconomySnapshot();
uint64_t EconomyReceivedMs();
bool CashAuthorityOnline();
uint64_t NextCashSequence();
Writer EconomyRequest(street::ClientOp operation);
bool SetServerCash(uint8_t player,int32_t amount);
bool DecideCashIncome(uint64_t request,bool approve);
// Native save/profile restoration must not overwrite the authoritative balance.
void RefreshAuthoritativeCash();
int64_t NativeCashFlow();  // excludes server-admin edits, escrow, refunds and payouts
}
