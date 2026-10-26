#include "features/balance/Balance.h"
#include "entities/economy/Economy.h"
namespace coop {
bool SetPlayerBalance(uint8_t target,int32_t amount) { return SetServerCash(target,amount); }
}
