// The destination game checks readiness automatically before either player moves.
#include "features/teleport/Teleport.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "common/api/protocol/Protocol.h"
#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "entities/player/Players.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"

namespace coop::teleport {

namespace {
enum : uint8_t { OP_GO = 1, OP_BRING = 2, OP_READY = 3, OP_UNAVAILABLE = 4 };
constexpr uint64_t kTimeoutMs = 20000;

struct Request {
    uint8_t who = 0xFF;  // the other player
    bool bring = false;  // /tphere (else /tp)
    uint64_t until = 0;
};
std::vector<Request> g_asked;  // ours, each until its answer
auto AskedOf(uint8_t who) {
    return std::find_if(g_asked.begin(), g_asked.end(), [who](const Request& q) { return q.who == who; });
}
void Send(uint8_t op, uint8_t to) {
    Writer w(proto::G_TP_ASK);
    w.U8(op);
    w.U8(to);
    Net().Send(proto::CH_RELIABLE, w);
}

void Handle(uint8_t from, Reader& r) {
    uint8_t op = r.U8(), to = r.U8();
    if (!r.Ok() || to != Net().MyId() || !Net().Peers().count(from)) return;
    std::string name = Net().PeerName(from);
    if (op == OP_GO || op == OP_BRING) {
        // Only the destination game knows if a mission parked its player outside the world.
        bool ready = core::IsPlayerReady() && (from == Net().HostId() || Net().PartnerTeleport()) &&
                     (op != OP_GO || !OwnCarFrozen());
        Send(ready ? OP_READY : OP_UNAVAILABLE, from);
        return;
    }
    auto asked = AskedOf(from);
    if (asked == g_asked.end() || (op != OP_READY && op != OP_UNAVAILABLE)) return;
    Request q = *asked;
    g_asked.erase(asked);
    if (op == OP_UNAVAILABLE || NowMs() >= q.until) {
        ui::TheChat().AddSystem(Format(TR("Сейчас перемещение с игроком %s недоступно.", "Teleport with %s is unavailable right now."), name.c_str()));
        return;
    }
    const RemotePlayer* rp = Players::Instance().Get(q.who);
    if (!rp || !rp->hasState || !core::IsPlayerReady() || !Players::Instance().TeleportAllowed(*rp, q.bring)) return;
    if (q.bring) Players::Instance().Bring(q.who);
    else Players::Instance().GoToPlayer(*rp);
}

class Teleport : public Module {
public:
    const char* Name() const override { return "Teleport"; }
    void OnInstallHooks() override { Net().SetHandler(proto::G_TP_ASK, &Handle); }
    void OnSessionEnd() override { g_asked.clear(); }
    void OnPeerLeave(uint8_t id) override {
        std::erase_if(g_asked, [id](const Request& q) { return q.who == id; });
    }
    void OnPostProcess() override {
        for (auto it = g_asked.begin(); it != g_asked.end();) {
            if (NowMs() < it->until) {
                ++it;
                continue;
            }
            ui::TheChat().AddSystem(Format(TR("Игра %s не ответила на команду перемещения.", "No response from %s's game to the teleport command."),
                                           Net().PeerName(it->who).c_str()));
            it = g_asked.erase(it);
        }
    }
    bool OnCommand(const std::string& cmd, const std::vector<std::string>& args) override { return Command(cmd, args); }

    static Teleport& Instance() {
        static Teleport t;
        return t;
    }
};

struct Reg {
    Reg() { core::RegisterModule(&Teleport::Instance()); }
} g_reg;
}  // namespace

bool Command(const std::string& cmd, const std::vector<std::string>& args) {
    if ((cmd != "tp" && cmd != "tphere") || !Net().Online()) return false;
    if (!core::IsPlayerReady()) return true;
    if (!Net().IsHost() && !Net().PartnerTeleport()) {
        ui::TheChat().AddError(TR("Хост запретил напарникам команды /tp и /tphere.", "The host turned /tp and /tphere off for partners."));
        return true;
    }
    // whom: its number (or nickname), the only other player with none given
    uint8_t who = 0xFF;
    for (auto& [id, peer] : Net().Peers())
        if (args.empty() ? Net().Peers().size() == 1 : args[0] == std::to_string(id) || !_stricmp(peer.name.c_str(), args[0].c_str())) who = id;
    if (who == 0xFF && !args.empty()) {
        ui::TheChat().AddError(Format(TR("Игрока %s нет в игре.", "There is no player %s in the game."), args[0].c_str()));
        return true;
    }
    if (who == 0xFF && Net().Peers().size() > 1) {
        std::string list;
        for (auto& [id, peer] : Net().Peers()) list += Format("%s/%s %d (%s)", list.empty() ? "" : ", ", cmd.c_str(), id, peer.name.c_str());
        ui::TheChat().AddSystem(Format(TR("Кого? Напишите номер игрока: %s.", "Whom? Type the player's number: %s."), list.c_str()));
        return true;
    }
    const RemotePlayer* target = Players::Instance().Get(who);
    if (!target || !target->hasState) {
        ui::TheChat().AddError(TR("Напарник ещё не в игре.", "Your partner is not in the game yet."));
        return true;
    }
    bool bring = cmd == "tphere";
    if (!Players::Instance().TeleportAllowed(*target, bring)) return true;
    std::string name = Net().PeerName(target->id);
    if (AskedOf(target->id) != g_asked.end()) {
        ui::TheChat().AddError(Format(TR("Перемещение с игроком %s уже выполняется.", "Teleport with %s is already in progress."),
                                      name.c_str()));
        return true;
    }
    Send(bring ? OP_BRING : OP_GO, target->id);
    g_asked.push_back({target->id, bring, NowMs() + kTimeoutMs});
    LOGI("Teleport request to %s (%s)", name.c_str(), cmd.c_str());
    return true;
}

}  // namespace coop::teleport
