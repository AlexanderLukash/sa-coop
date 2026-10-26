#include "features/balance/Balance.h"

#include <map>
#include "common/api/protocol/Protocol.h"
#include "common/lib/util/Util.h"
#include "entities/profile/Profile.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"

namespace coop {
namespace {
std::map<uint8_t, int32_t> g_pending;

void ApplyBalance(int32_t amount) {
    auto& info = game::PlayerInfos()[0];
    info.money = info.displayMoney = amount;
    if (!Net().IsHost()) SaveProfileNow();
    ui::TheChat().AddSystem(Format(TR("Баланс установлен: $%d.", "Balance set to $%d."), amount));
}

class Balance : public Module {
public:
    const char* Name() const override { return "Balance"; }
    void OnInstallHooks() override {
        Net().SetHandler(proto::G_PLAYER_BALANCE, [](uint8_t from, Reader& r) {
            proto::BalanceChange change;
            if (!change.Read(r) || !Net().Online() || from != Net().HostId() || change.target != Net().MyId()) return;
            if (!core::IsPlayerReady()) return;
            ApplyBalance(change.amount);
            Writer w(proto::G_BALANCE_RESULT);
            w.I32(change.amount);
            Net().Send(proto::CH_RELIABLE, w);
        });
        Net().SetHandler(proto::G_BALANCE_RESULT, [](uint8_t from, Reader& r) {
            int32_t amount = r.I32();
            auto it = g_pending.find(from);
            if (!r.Ok() || r.Remaining() || !Net().IsHost() || it == g_pending.end() || it->second != amount) return;
            g_pending.erase(it);
            ui::TheChat().AddSystem(Format(TR("Баланс %s установлен: $%d.", "Balance for %s set to $%d."),
                                          Net().PeerName(from).c_str(), amount));
        });
    }
    void OnSessionEnd() override { g_pending.clear(); }
    void OnPeerLeave(uint8_t id) override { g_pending.erase(id); }
};
struct Reg {
    Reg() { static Balance module; core::RegisterModule(&module); }
} g_reg;
}

bool SetPlayerBalance(uint8_t target, int32_t amount) {
    if (!Net().IsHost() || !core::IsPlayerReady() || amount < 0 || amount > 999999999 ||
        (target != Net().MyId() && !Net().Peer(target))) return false;
    if (target == Net().MyId()) {
        ApplyBalance(amount);
    } else {
        g_pending[target] = amount;
        Writer w(proto::G_PLAYER_BALANCE);
        proto::BalanceChange{target, amount}.Write(w);
        Net().Send(proto::CH_RELIABLE, w);
        ui::TheChat().AddSystem(TR("Запрос изменения баланса отправлен.", "Balance change requested."));
    }
    return true;
}
}
