// Replication of animations that the pad replay cannot reproduce.
//
// Idle animations: CTaskSimplePlayerOnFoot::PlayIdleAnimations (0x6872C0) starts a random
// "playidles" animation after 20 s of inactivity, judged by the *local* pad touch timer. For the
// remote player ped this would use the wrong timer and a different random choice, so the function is
// skipped for it; instead the owner reports the idle animation it plays and the other side blends the
// same animation on the remote ped.
//
// The same channel (G_PLAYER_ANIM) is used for other one-shot animations (see kReplicatedGroups).
#include <algorithm>

#include "common/api/protocol/Protocol.h"
#include "common/lib/log/Log.h"
#include "entities/player/Players.h"
#include "shared/api/net-session/Session.h"
#include "shared/game/Anim.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"
#include "shared/lib/memory/Memory.h"

namespace coop {

namespace {
using namespace game;

constexpr int ANIM_GROUP_PLAYIDLES = 49;
constexpr uint16_t ANIMATION_IDLE_FLAG = 0x200;  // set by PlayIdleAnimations on its animations

enum : uint8_t { OP_PLAY = 1, OP_STOP_IDLE = 2 };

// groups whose animations are replicated as events (they are not produced by the pad replay)
constexpr int kReplicatedGroups[] = {ANIM_GROUP_PLAYIDLES};

bool IsReplicatedGroup(int g) {
    for (int r : kReplicatedGroups)
        if (r == g) return true;
    return false;
}

bool EnsureBlockLoaded(const char* block) {
    int idx = Call<0x4D3990, int>(block);  // CAnimManager::GetAnimationBlockIndex
    if (idx < 0) return false;
    if (anim::BlockLoaded(idx)) return true;
    RequestModel(anim::kIfpBase + idx, 2);
    LoadAllRequestedModels(false);
    return anim::BlockLoaded(idx);
}

const char* BlockForGroup(int group) { return group == ANIM_GROUP_PLAYIDLES ? "playidles" : nullptr; }

using PlayIdleFn = void(__fastcall*)(void*, void*, CPlayerPed*);
PlayIdleFn g_origPlayIdle = nullptr;
void __fastcall HookPlayIdleAnimations(void* task, void* edx, CPlayerPed* player) {
    if (Players::Instance().FindByPed(player)) return;  // remote player: idles come from the network
    g_origPlayIdle(task, edx, player);
}

class Anims : public Module {
public:
    const char* Name() const override { return "Anims"; }

    void OnInstallHooks() override {
        mem::HookFn(0x6872C0, &HookPlayIdleAnimations, &g_origPlayIdle, "CTaskSimplePlayerOnFoot::PlayIdleAnimations");
        Net().SetHandler(proto::G_PLAYER_ANIM, [](uint8_t from, Reader& r) { Instance().Handle(from, r); });
    }

    void OnPostProcess() override {
        // in a cutscene both games animate the partner themselves (ScenePartner), idle variations included
        if (!Net().Online() || !core::IsPlayerReady() || Ref<uint8_t>(0xB5F851)) return;  // CCutsceneMgr::ms_running
        CPlayerPed* me = FindPlayerPed(0);
        // the replicated animation currently playing on the local player (blending in or full)
        int group = -1, id = -1;
        for (void* a = anim::First(me->RwObject()); a; a = anim::Next(a)) {
            if (!IsReplicatedGroup(anim::Group(a)) || anim::BlendDelta(a) < 0.0f) continue;
            group = anim::Group(a);
            id = anim::Id(a);
            break;
        }
        if (group == m_lastGroup && id == m_lastId) return;
        Writer w(proto::G_PLAYER_ANIM);
        if (id >= 0) {
            w.U8(OP_PLAY);
            w.I16((int16_t)group);
            w.I16((int16_t)id);
            w.F32(8.0f);
        } else {
            w.U8(OP_STOP_IDLE);
        }
        Net().Send(proto::CH_RELIABLE, w);
        LOGI("Local animation %s: group %d id %d", id >= 0 ? "started" : "stopped", id >= 0 ? group : m_lastGroup,
             id >= 0 ? id : m_lastId);
        m_lastGroup = group;
        m_lastId = id;
    }

    void Handle(uint8_t from, Reader& r) {
        uint8_t op = r.U8();
        RemotePlayer* rp = Players::Instance().Get(from);
        if (!rp || !rp->ped || !core::IsPlayerReady()) return;
        void* clump = rp->ped->RwObject();
        if (!clump) return;
        if (op == OP_PLAY) {
            int group = r.I16();
            int id = r.I16();
            float delta = r.F32();
            if (!r.Ok() || !IsReplicatedGroup(group)) return;
            const char* block = BlockForGroup(group);
            if (block && !EnsureBlockLoaded(block)) return;
            uintptr_t grp = Ref<uintptr_t>(0xB4EA34) + (uintptr_t)group * 0x14;  // CAnimManager::ms_aAnimAssocGroups
            int first = (int)Ref<uint32_t>(grp + 0x0C), count = (int)Ref<uint32_t>(grp + 0x08);  // m_IdOffset, m_NumAnims
            if (id < first || id >= first + count) return;  // from the network
            void* a = Call<0x4D4610, void*>(clump, group, id, std::clamp(delta, 1.0f, 50.0f));  // CAnimManager::BlendAnimation
            if (a) anim::Flags(a) |= ANIMATION_IDLE_FLAG;
            LOGI("Remote player %d animation: group %d id %d (%s)", from, group, id, a ? "ok" : "failed");
        } else if (op == OP_STOP_IDLE) {
            for (void* a = anim::First(clump); a; a = anim::Next(a))
                if ((anim::Flags(a) & ANIMATION_IDLE_FLAG) && IsReplicatedGroup(anim::Group(a))) {
                    anim::BlendDelta(a) = -8.0f;
                    anim::Flags(a) |= anim::F_AUTO_REMOVE;  // deleted once out
                }
        }
    }

    void OnGameStarted() override { m_lastGroup = m_lastId = -1; }

    static Anims& Instance() {
        static Anims a;
        return a;
    }

private:
    int m_lastGroup = -1, m_lastId = -1;
};

struct Reg {
    Reg() { core::RegisterModule(&Anims::Instance()); }
} g_reg;
}  // namespace

}  // namespace coop
