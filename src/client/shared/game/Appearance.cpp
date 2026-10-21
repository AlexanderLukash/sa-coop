#include "shared/game/Appearance.h"
#include "shared/game/Script.h"
#include "shared/game/SkinCatalog.h"
#include "shared/lib/memory/Memory.h"

namespace coop::appearance {
using namespace game;
using game::script::Cmd;

namespace {
using RebuildPlayerFn = void(__cdecl*)(CPlayerPed*, bool);
RebuildPlayerFn g_rebuildPlayer = nullptr;
void __cdecl RebuildCjOnly(CPlayerPed* ped, bool keepDescFatMuscle) {
    // Vanilla checks fat/muscle changes even after SET_PLAYER_MODEL. Building
    // CJ's clothes onto a normal ped replaces its mesh and mismatches its rig.
    if (ped && ped->ModelIndex() == 0) g_rebuildPlayer(ped, keepDescFatMuscle);
}
}

void InstallHooks() {
    mem::HookFn(0x5A82C0, &RebuildCjOnly, &g_rebuildPlayer, "CClothes::RebuildPlayer (CJ only)");
}

// Logical skin IDs are independent of GTA's ten shared special-character slots.
// Keep referenced models intact, including characters owned by story scripts.
struct SpecialSkin { int skin = -1; uint32_t key = 0; };
SpecialSkin g_specialSkins[10];
void* SkinInfo(int model) { return Ref<void*>(addr::ModelInfoPtrs + (uintptr_t)model * 4); }
int LogicalSkin(int model) {
    if (model >= 290 && model < 300) {
        auto& cached = g_specialSkins[model - 290];
        void* info = SkinInfo(model);
        return info && cached.skin >= 0 && Field<uint32_t>(info, 4) == cached.key ? cached.skin : 0;
    }
    const auto* skin = skins::Find(model);
    return skin && !skin->special ? model : 0;
}
int ResolveSkin(int id) {
    const auto* skin = skins::Find(id);
    if (!skin) return -1;
    if (!skin->special) return id;
    for (int i = 0; i < 10; ++i) {
        void* info = SkinInfo(290 + i);
        if (info && g_specialSkins[i].skin == id && Field<uint32_t>(info, 4) == g_specialSkins[i].key && IsModelLoaded(290 + i)) return 290 + i;
    }
    for (int i = 0; i < 10; ++i) {
        const int model = 290 + i;
        void* info = SkinInfo(model);
        if (!info || Field<uint16_t>(info, 8) != 0) continue;
        // Do not take a loaded slot belonging to a story script or another mod.
        const uint8_t streamingState = Ref<uint8_t>(addr::StreamingInfo + (uintptr_t)model * 0x14 + 0x10);
        if (streamingState != 0 && (g_specialSkins[i].skin < 0 || Field<uint32_t>(info, 4) != g_specialSkins[i].key)) continue;
        Call<0x409D10>(model, skin->model, 2 | 0x10); // CStreaming::RequestSpecialModel
        LoadAllRequestedModels(false);
        if (!IsModelLoaded(model)) return -1;
        g_specialSkins[i] = {id, Field<uint32_t>(info, 4)};
        return model;
    }
    return -1; // Try again later; never replace a live story/player clump.
}

bool ApplySkin(CPlayerPed* ped, int model) {
    model = ResolveSkin(model);
    if (model < 0 || !IsPedModel(model)) return false;
    if (ped->ModelIndex() == model) return true;
    if (!LoadModelNow(model)) return false;
    // SET_PLAYER_MODEL performs the engine's clump/animation teardown. Remote peds
    // borrow slot 1 for this synchronous command, just as their control processing does.
    int slot = ped == FindPlayerPed(0) ? 0 : 1;
    CPlayerPed* kept = PlayerInfo(slot)->ped;
    PlayerInfo(slot)->ped = ped;
    Cmd(0x09C7).I(slot).I(model).Run();
    PlayerInfo(slot)->ped = kept;
    return ped->ModelIndex() == model;
}

}  // namespace coop::appearance
