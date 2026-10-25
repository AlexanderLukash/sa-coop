// Property flags and entrances still follow the shared host world, but each ordinary safe house has a personal owner.
// The partner's purchased-house bitset lives in its server profile; owner snapshots and labels are session-wide.
// Once the player buys a for-sale pickup, the game's buying threads
// (BUY_PRO, BUY1-3) launch BUYPRO1 with the property's index in $6940, and it shows the house and applies the
// purchase. Each game reports its own purchases and applies the other player's:
//  * an ordinary house (3..31) quietly, as BUYPRO1 does it but without its camera: the bought flag, no for-sale
//    pickup, the save house blip, the entrance and the garage;
//  * the three story purchases (0 Wang Cars, 1 Zero's RC shop, 2 Verdant Meadows) start mission chains, so only the
//    host's game applies the partner's one, with BUYPRO1 itself once the host is free. The partner gets the host's
//    ones with the host's world.
// After a world load the partner applies his purchases again: one made just before the host saved its world is not in it.
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "common/api/protocol/Protocol.h"
#include "common/lib/log/Log.h"
#include "common/lib/sync/StateTiming.h"
#include "common/lib/util/Util.h"
#include "entities/profile/Profile.h"
#include "entities/session/Flow.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"
#include "shared/ui/renderer/Renderer.h"

namespace coop {

namespace {
using namespace game;
using game::script::Cmd;

constexpr int kProperties = 32;
// the garages BUYPRO1 activates (none for the rest)
const char* const kGarage[kProperties] = {nullptr, nullptr, nullptr, "BEACSV", "VESVGRG", "CN2GAR1", "BLOB69", "BLOB7",
                                          "BURBDOO", "BLOB6", "CARLAS1", "SAV1SFE", "CESAFE1", "SAV1SFW", "SVGSFS1",
                                          nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                          nullptr, "BURBDO2"};

// main.scm globals (byte offsets), the arrays indexed by the property: bought flags $2912, for-sale pickups $6740,
// blips $6484, position $6096/$6224/$6352
int32_t& Global(int offset) { return Ref<int32_t>(addr::ScriptSpace + offset); }
float GlobalF(int offset) { return Ref<float>(addr::ScriptSpace + offset); }
int32_t& Bought(int k) { return Global(2912 + 4 * k); }

void* FindScript(const char* name) {
    for (void* s = Ref<void*>(0xA8B42C); s; s = Field<void*>(s, 0x00))  // CTheScripts::pActiveScripts
        if (!strncmp((const char*)s + 0x08, name, 8)) return s;
    return nullptr;
}

std::vector<std::pair<uint8_t, int>> g_pending;  // (player, property) bought by the others, to apply here
std::set<int> g_mine;                             // partner: bought here this session
std::map<int, uint8_t> g_owner;                   // ordinary safe house -> current session owner
uint32_t g_hostMine = 0;
uint32_t g_lastOwnershipMask = UINT32_MAX;
bool g_buying = false;                            // a BUYPRO1 runs
bool g_ours = false;                              // ... launched here for the host's partner
int g_buyIndex = -1;                              // ... for this property ($6940 when it began)

uint32_t LocalMask() {
    uint32_t mask = Net().IsHost() ? g_hostMine : PersonalPropertyMask();
    if (Net().IsHost())
        for (const auto& [k, owner] : g_owner)
            if (owner != Net().MyId()) mask &= ~(1u << k);
    return mask;
}

void SendOwnership() {
    if (!Net().Online()) return;
    uint32_t mask = LocalMask();
    if (mask == g_lastOwnershipMask) return;
    Writer w(proto::G_PROPERTY_OWNERS);
    w.U32(mask);
    Net().Send(proto::CH_RELIABLE, w);
    g_lastOwnershipMask = mask;
}

void HandleOwnership(uint8_t from, Reader& r) {
    uint32_t mask = r.U32();
    if (!r.Ok() || from == Net().MyId() || (mask & 7u)) return;
    for (auto it = g_owner.begin(); it != g_owner.end();) {
        if (it->second == from) it = g_owner.erase(it);
        else ++it;
    }
    for (int k = 3; k < kProperties; ++k)
        if (mask & (1u << k)) g_owner[k] = from;
}

uint32_t BoughtMask() {
    uint32_t mask = 0;
    for (int k = 3; k < kProperties; ++k)
        if (Bought(k)) mask |= 1u << k;
    return mask;
}

void ApplyQuietly(int k) {
    Bought(k) = 1;
    if (int pickup = Global(6740 + 4 * k)) Cmd(0x0215).I(pickup).Run();  // REMOVE_PICKUP: the for-sale one
    int32_t& blip = Global(6484 + 4 * k);
    float x = GlobalF(6096 + 4 * k), y = GlobalF(6224 + 4 * k), z = GlobalF(6352 + 4 * k);
    if (blip) Cmd(0x0164).I(blip).Run();                // REMOVE_BLIP
    Cmd(0x0570).F(x).F(y).F(z).I(35).Out(&blip).Run();  // ADD_SHORT_RANGE_SPRITE_BLIP_FOR_CONTACT_POINT: a save house
    Cmd(0x018B).I(blip).I(2).Run();                     // CHANGE_BLIP_DISPLAY: radar only
    Cmd(0x09B4).F(x).F(y).F(k == 31 ? 5.0f : 10.0f).I(16384).I(1).Run();  // SET_CLOSEST_ENTRY_EXIT_FLAG: the door opens
    if (kGarage[k]) Cmd(0x0299).S8(kGarage[k]).Run();                     // ACTIVATE_GARAGE
    Cmd(0x030C).I(1).Run();                                               // PLAYER_MADE_PROGRESS
}

// a story purchase, as its BUYn thread makes it; false while the host is busy
bool LaunchStoryPurchase(int k, bool& launched) {
    CPlayerPed* me = FindPlayerPed(0);
    if (core::IsOnMission() || me->InVehicle() || me->AreaCode() != 0 || Ref<uint8_t>(0xB5F851)) return false;  // CCutsceneMgr::ms_running
    char thread[8];
    snprintf(thread, sizeof(thread), "buy%d", k + 1);
    launched = FindScript(thread) != nullptr;  // gone: bought here already
    if (!launched) return true;
    Global(6940) = k;
    Cmd(0x0459).S8(thread).Run();  // TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME
    if (int pickup = Global(6740 + 4 * k)) Cmd(0x0215).I(pickup).Run();  // REMOVE_PICKUP
    Cmd(0x0417).I(134).Run();      // LOAD_AND_LAUNCH_MISSION_INTERNAL BUYPRO1
    g_ours = true;
    return true;
}

void Send(int k) {
    Writer w(proto::G_PROPERTY);
    w.U8((uint8_t)k);
    Net().Send(proto::CH_RELIABLE, w);
}

void MarkOwnedHere(int k) {
    if (k < 3 || k >= kProperties) return;  // story properties are shared progression
    g_owner[k] = Net().MyId();
    if (Net().IsHost()) g_hostMine |= 1u << k;
    else {
        uint32_t mask = PersonalPropertyMask() | (1u << k);
        SetPersonalPropertyMask(mask);
        SaveProfileNow();
    }
}

class Property : public Module {
public:
    const char* Name() const override { return "Property"; }

    void OnInstallHooks() override {
        Net().SetHandler(proto::G_PROPERTY, [](uint8_t from, Reader& r) {
            int k = r.U8();
            if (r.Ok() && k < kProperties && flow::InSharedWorld()) {
                if (k >= 3) g_owner[k] = from;
                g_pending.emplace_back(from, k);
            }
        });
        Net().SetHandler(proto::G_PROPERTY_OWNERS, &HandleOwnership);
    }
    void OnSessionEnd() override {
        g_pending.clear();
        g_mine.clear();
        g_owner.clear();
        g_hostMine = 0;
        g_lastOwnershipMask = UINT32_MAX;
    }
    void OnGameStarted() override {
        g_buying = FindScript("buypro1") != nullptr;
        g_ours = false;
        if (Net().IsHost()) {
            g_hostMine = BoughtMask();
            for (auto it = g_owner.begin(); it != g_owner.end();) {
                if (it->second == Net().MyId() && !(g_hostMine & (1u << it->first))) it = g_owner.erase(it);
                else ++it;
            }
        }
        if (!Net().Online() || Net().IsHost()) return;
        uint32_t personal = PersonalPropertyMask();
        for (int k = 3; k < kProperties; ++k)
            if (personal & (1u << k)) g_mine.insert(k), g_owner[k] = Net().MyId();
        for (int k : g_mine) {
            if (Bought(k)) continue;
            ApplyQuietly(k);
            Send(k);
            LOGI("Property %d: bought here before the world load, applied again", k);
        }
    }
    void OnPeerJoin(uint8_t) override {
        g_lastOwnershipMask = UINT32_MAX;
        SendOwnership();
    }

    void OnPostProcess() override {
        if (!Net().Online() || !core::IsPlayerReady()) return;
        if (Net().IsHost() && !g_hostMine) g_hostMine = BoughtMask();
        if (Net().IsHost())
            for (int k = 3; k < kProperties; ++k)
                if (g_hostMine & (1u << k)) g_owner.try_emplace(k, Net().MyId());
        if (!Net().IsHost()) {
            uint32_t personal = PersonalPropertyMask();
            for (auto it = g_owner.begin(); it != g_owner.end();) {
                if (it->second == Net().MyId() && !(personal & (1u << it->first))) it = g_owner.erase(it);
                else ++it;
            }
            for (auto it = g_mine.begin(); it != g_mine.end();) {
                if (!(personal & (1u << *it))) it = g_mine.erase(it);
                else ++it;
            }
            for (int k = 3; k < kProperties; ++k) {
                if (!(personal & (1u << k))) continue;
                g_mine.insert(k);
                g_owner.try_emplace(k, Net().MyId());
                if (!Bought(k)) {
                    ApplyQuietly(k);
                    Send(k);  // a personal home missing from the host's latest world snapshot
                }
            }
        }
        SendOwnership();  // also publishes profile restores and new-game resets
        // a purchase made here
        bool buying = FindScript("buypro1") != nullptr;
        if (buying && !g_buying) g_buyIndex = Global(6940);
        // a story purchase belongs to the story: the partners get the host's world after it, as after a mission (it was
        // no mission for them, and they could buy the property again)
        if (!buying && g_buying && Net().IsHost() && g_buyIndex >= 0 && g_buyIndex < 3) flow::WorldProgressed();
        if (buying && !g_buying && !g_ours) {
            int k = Global(6940);
            if (k >= 0 && k < kProperties && flow::InSharedWorld()) {
                Send(k);
                if (!Net().IsHost()) g_mine.insert(k);
                MarkOwnedHere(k);
                LOGI("Property %d bought here", k);
            }
        }
        if (!buying) g_ours = false;
        g_buying = buying;

        for (auto it = g_pending.begin(); it != g_pending.end();) {
            auto [from, k] = *it;
            bool applied = false;
            if (k >= 3) {
                applied = !Bought(k);
                if (applied) ApplyQuietly(k);
            } else if (Net().IsHost() && !LaunchStoryPurchase(k, applied)) {
                ++it;
                continue;
            }  // a story purchase of the host comes to the partner with the host's world
            LOGI("Property %d bought by player %d: %s", k, from, applied ? "applied" : "not applied here");
            if (applied)
                ui::TheChat().AddSystem(Format(TR("Гравець %s купив дім %d; власника видно біля входу і в /homes.",
                                                  "Player %s bought home %d; its owner is shown at the entrance and in /homes."),
                                               Net().PeerName(from).c_str(), k));
            it = g_pending.erase(it);
        }
    }

    void OnRender() override {
        if (!Net().Online() || !core::IsPlayerReady()) return;
        CPlayerPed* me = FindPlayerPed(0);
        if (!me || me->AreaCode() != 0) return;
        auto& gfx = ui::Gfx();
        for (int k = 3; k < kProperties; ++k) {
            auto own = g_owner.find(k);
            if (own == g_owner.end() || !Bought(k)) continue;
            float x = GlobalF(6096 + 4 * k), y = GlobalF(6224 + 4 * k), z = GlobalF(6352 + 4 * k);
            if (!sync::PlausiblePosition({x, y, z}) || (std::fabs(x) + std::fabs(y) < 1.0f)) continue;
            CVector pos{x, y, z + 1.2f};
            float distance = (pos - me->GetPosition()).Length();
            if (distance > 90.0f) continue;
            float sx, sy, depth;
            if (!WorldToScreen(pos, sx, sy, depth)) continue;
            sx *= gfx.Width() / (float)std::max(1, ScreenWidth());
            sy *= gfx.Height() / (float)std::max(1, ScreenHeight());
            float alpha = distance > 65.0f ? 1.0f - (distance - 65.0f) / 25.0f : 1.0f;
            std::string owner = Net().PeerName(own->second);
            std::string label = Format(TR("Будинок %d · %s", "Home %d · %s"), k, owner.c_str());
            uint32_t color = own->second == Net().HostId() ? 0xFF64B4FF : 0xFF78DC78;
            gfx.Text(ui::FONT_SMALL, sx, sy, label, ui::WithAlpha(color, alpha), ui::TEXT_CENTER | ui::TEXT_OUTLINE);
        }
    }

    bool OnCommand(const std::string& cmd, const std::vector<std::string>&) override {
        if (cmd != "homes") return false;
        if (g_owner.empty()) {
            ui::TheChat().AddSystem(TR("В этой сессии ещё нет домов с известным владельцем.",
                                       "No homes with a known owner in this session yet."));
            return true;
        }
        for (const auto& [k, owner] : g_owner) {
            if (!Bought(k)) continue;
            std::string who = owner == Net().MyId() ? TR("вы", "you") : Net().PeerName(owner);
            std::string state = owner != Net().MyId() && Net().Peers().count(owner)
                                    ? TR("онлайн", "online")
                                    : (owner == Net().MyId() ? TR("ваш", "yours") : TR("не в сессии", "offline"));
            ui::TheChat().AddSystem(Format(TR("Дом %d — %s (%s)", "Home %d — %s (%s)"), k, who.c_str(), state.c_str()));
        }
        return true;
    }

    static Property& Instance() {
        static Property m;
        return m;
    }
};

struct Reg {
    Reg() { core::RegisterModule(&Property::Instance()); }
} g_reg;
}  // namespace

}  // namespace coop
