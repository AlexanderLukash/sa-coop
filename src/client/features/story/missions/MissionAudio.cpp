// Mirror GTA's four mission dialogue slots, including asynchronous bank loading.
#include <array>

#include "common/api/protocol/Protocol.h"
#include "entities/mission/Mission.h"
#include "common/lib/util/Util.h"
#include "shared/api/net-session/Session.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/lib/memory/Memory.h"

namespace coop {
namespace {
using namespace game;
constexpr uintptr_t kAudioEngine = 0xB6BC90;
enum : uint8_t { LOAD = 1, PLAY = 2, CLEAR = 3 };
struct Slot { bool owned = false, pending = false; uint64_t playMs = 0; };
std::array<Slot, 4> g_slots{};
using LoadFn = void(__fastcall*)(void*, void*, uint8_t, int32_t);
using SlotFn = void(__fastcall*)(void*, void*, uint8_t);
LoadFn g_load = nullptr;
SlotFn g_play = nullptr, g_clear = nullptr;

void Send(uint8_t op, uint8_t slot, int32_t event = 0) {
    if (slot >= g_slots.size() || !HostReplicating()) return;
    Writer w(proto::G_MISSION_AUDIO);
    w.U8(op);
    w.U8(slot);
    if (op == LOAD) w.I32(event);
    Net().Send(proto::CH_RELIABLE, w);
}
void __fastcall Load(void* self, void* edx, uint8_t slot, int32_t event) {
    g_load(self, edx, slot, event);
    Send(LOAD, slot, event);
}
void __fastcall Play(void* self, void* edx, uint8_t slot) {
    g_play(self, edx, slot);
    Send(PLAY, slot);
}
void __fastcall Clear(void* self, void* edx, uint8_t slot) {
    g_clear(self, edx, slot);
    Send(CLEAR, slot);
}
void Reset() {
    for (uint8_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i].owned && core::IsPlayerReady()) g_clear((void*)kAudioEngine, nullptr, i);
        g_slots[i] = {};
    }
}
void Receive(uint8_t from, Reader& r) {
    uint8_t op = r.U8(), slot = r.U8();
    int32_t event = op == LOAD ? r.I32() : 0;
    if (!r.Ok() || from != Net().HostId() || Net().IsHost() ||
        !core::IsPlayerReady() || slot >= g_slots.size()) return;
    Slot& s = g_slots[slot];
    if (op == LOAD) {
        g_load((void*)kAudioEngine, nullptr, slot, event);
        s = {true, false, 0};
    } else if (op == PLAY && s.owned) {
        s.pending = true;
        s.playMs = NowMs();
    } else if (op == CLEAR && s.owned) {
        g_clear((void*)kAudioEngine, nullptr, slot);
        s = {};
    }
}
class MissionAudio : public Module {
public:
    const char* Name() const override { return "MissionAudio"; }
    void OnInstallHooks() override {
        mem::HookFn(0x507290, &Load, &g_load, "CAudioEngine::PreloadMissionAudio");
        mem::HookFn(0x5072B0, &Play, &g_play, "CAudioEngine::PlayLoadedMissionAudio");
        mem::HookFn(0x5072F0, &Clear, &g_clear, "CAudioEngine::ClearMissionAudio");
        Net().SetHandler(proto::G_MISSION_AUDIO, &Receive);
    }
    void OnPostProcess() override {
        if (!Net().Online() || Net().IsHost() || !core::IsPlayerReady()) return;
        for (uint8_t i = 0; i < g_slots.size(); ++i) {
            Slot& s = g_slots[i];
            if (!s.pending) continue;
            if (NowMs() - s.playMs > 5000) { s.pending = false; continue; }
            if (script::Cmd(0x03D0).I(i).Run()) { // HAS_MISSION_AUDIO_LOADED
                g_play((void*)kAudioEngine, nullptr, i);
                s.pending = false;
            }
        }
    }
    void OnSessionEnd() override { Reset(); }
    void OnGameShutdown() override { Reset(); }
    void OnGameStarted() override { g_slots = {}; }
};
MissionAudio g_audio;
struct Reg { Reg() { core::RegisterModule(&g_audio); } } g_reg;
}
}
