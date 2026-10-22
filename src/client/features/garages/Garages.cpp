// The safe houses' garages, which keep the cars left in them, belong to the host's world. What they keep is part of
// the host's game and its save, and the partner's game loads that world again and again (story progress), so the
// partner's game neither puts cars away nor brings any out: both games doing it made every kept car twice, and a car
// the partner had put away was gone with the next world. The partner's game asks the host's instead. When a garage
// closes behind a car of the partner's, the host's game keeps it (as it would its own) and the partner's lets it go.
// When a garage opens for the partner, the host's game hands over what it keeps there and the partner's brings the
// cars out (where the partner is, the ground is loaded). The host's snapshots of its world for the partners are saves too: they no longer slam an open garage
// shut on the cars in it.
#include <cstring>
#include <array>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "common/api/protocol/Protocol.h"
#include "common/lib/log/Log.h"
#include "common/lib/sync/GarageRestore.h"
#include "entities/session/Flow.h"
#include "entities/vehicle/Vehicles.h"
#include "shared/api/net-session/Session.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/lib/memory/Memory.h"

namespace coop {

namespace {
using namespace game;
using game::script::Cmd;

constexpr uintptr_t kGarages = 0x96C048, kNumGarages = 0x96C024;  // CGarages::aGarages (CGarage 0xD8 each), NumGarages
constexpr uintptr_t kStoredCars = 0x96ABD8;                         // CGarages::aCarsInSafeHouse: 4 CStoredCar (0x40) a house
constexpr size_t kGarageSize = 0xD8;
enum : uint8_t {
    GARAGE_STORE = 1,
    GARAGE_KEPT = 2,
    GARAGE_BRING_OUT = 3,
    GARAGE_CARS = 4,      // legacy broadcast, retained for older peers
    GARAGE_BACK = 5,
    GARAGE_CARS_TO = 6,   // targeted restore: owner id, count, stored cars
};
constexpr size_t kStoredCarSize = 0x40;
constexpr uint8_t kGarageSlots = 4;
using GarageOwners = std::array<std::string, kGarageSlots>;  // stable peer names; empty means legacy/host car
std::map<uint8_t, GarageOwners> g_storedOwners;             // by safe-house storage index
std::map<uint16_t, std::vector<uint16_t>> g_pendingRestore;  // expected model indices, keyed by garage and peer

uint16_t PendingKey(uint8_t garage, uint8_t peer) { return (uint16_t(garage) << 8) | peer; }

uint8_t TypeOf(void* g) { return Field<uint8_t>(g, 0x4C); }  // CGarage::m_nType
bool IsHideout(uint8_t t) { return (t >= 16 && t <= 18) || (t >= 24 && t <= 32) || (t >= 39 && t <= 42) || t == 45; }
void* GarageAt(int i) {
    return i >= 0 && i < 50 && i < Ref<int32_t>(kNumGarages) ? (void*)(kGarages + (uintptr_t)i * kGarageSize) : nullptr;
}
uint8_t IndexOf(void* g) { return (uint8_t)(((uintptr_t)g - kGarages) / kGarageSize); }
std::string NameOf(void* g) { return std::string(Field<char[8]>(g, 0x44), strnlen(Field<char[8]>(g, 0x44), 8)); }
uint8_t* StoreOf(void* g) {  // CGarages::FindSafeHouseIndexForGarageType
    return (uint8_t*)(kStoredCars + (uintptr_t)Call<0x4489F0, int>((int)TypeOf(g)) * 0x100);
}
uint8_t StoreIndex(void* g) { return (uint8_t)((StoreOf(g) - (uint8_t*)kStoredCars) / 0x100); }
bool Inside(void* g, const CVector& p) { return CallMethod<0x448740, bool>(g, p); }  // CGarage::IsPointInsideGarage

// the partner's game in the host's world: its garages keep nothing of their own
bool Partner() { return Net().Online() && !Net().IsHost() && flow::InHostWorld(); }

bool Occupied(CVehicle* v) {
    if (v->Driver()) return true;
    for (int i = 0; i < 8; ++i)
        if (v->Passengers()[i]) return true;
    return false;
}

// what CGarage::StoreAndRemoveCarsForThisHideOut would put away of ours goes to the host's store; our cars stay here
// until the host has them
void AskToKeep(void* garage) {
    if (!core::IsPlayerReady()) return;  // a world loading: nothing of ours in it
    CPoolHeader* pool = VehiclePool();
    std::vector<uint16_t> ids;
    for (int i = 0; pool && i < pool->size && ids.size() < 4; ++i) {
        if (pool->byteMap[i] < 0) continue;
        auto* v = (CVehicle*)(pool->objects + (size_t)i * kVehiclePoolElemSize);
        NetVehicle* nv = Vehicles::Instance().Find(v);
        if (nv && !Vehicles::Instance().IsMirror(*nv) && !Occupied(v) && Inside(garage, v->GetPosition())) ids.push_back(nv->netId);
    }
    if (ids.empty()) return;
    Writer w(proto::G_GARAGE);
    w.U8(GARAGE_STORE);
    w.U8(IndexOf(garage));
    w.U8((uint8_t)ids.size());
    for (uint16_t id : ids) w.U16(id);
    Net().Send(proto::CH_RELIABLE, w);
    LOGI("Garage %s: %zu car(s) of ours go to the host's store", NameOf(garage).c_str(), ids.size());
}

using StoreFn = void(__fastcall*)(void*, void*, void*, int);
StoreFn g_origStoreClosing = nullptr, g_origStoreBeforeSave = nullptr;
void __fastcall HookStoreClosing(void* garage, void* edx, void* stored, int maxSlot) {  // the door closed (CGarage::Update)
    if (!Partner()) {
        uint8_t* records = StoreOf(garage);
        bool wasEmpty[kGarageSlots]{};
        for (int i = 0; i < kGarageSlots; ++i)
            wasEmpty[i] = Ref<uint16_t>((uintptr_t)records + (uintptr_t)i * kStoredCarSize + 0x12) == 0;
        g_origStoreClosing(garage, edx, stored, maxSlot);
        if (Net().Online() && Net().IsHost()) {
            auto& owners = g_storedOwners[StoreIndex(garage)];
            for (int i = 0; i < kGarageSlots; ++i)
                if (wasEmpty[i] && Ref<uint16_t>((uintptr_t)records + (uintptr_t)i * kStoredCarSize + 0x12))
                    owners[i] = Net().PeerName(Net().MyId());
        }
        return;
    }
    AskToKeep(garage);
}
void __fastcall HookStoreBeforeSave(void* garage, void* edx, void* stored, int maxSlot) {  // CloseHideOutGaragesBeforeSave
    if (!Partner()) {
        uint8_t* records = StoreOf(garage);
        bool wasEmpty[kGarageSlots]{};
        for (int i = 0; i < kGarageSlots; ++i)
            wasEmpty[i] = Ref<uint16_t>((uintptr_t)records + (uintptr_t)i * kStoredCarSize + 0x12) == 0;
        g_origStoreBeforeSave(garage, edx, stored, maxSlot);
        if (Net().Online() && Net().IsHost()) {
            auto& owners = g_storedOwners[StoreIndex(garage)];
            for (int i = 0; i < kGarageSlots; ++i)
                if (wasEmpty[i] && Ref<uint16_t>((uintptr_t)records + (uintptr_t)i * kStoredCarSize + 0x12))
                    owners[i] = Net().PeerName(Net().MyId());
        }
        return;
    }
    AskToKeep(garage);
}

using RestoreFn = bool(__fastcall*)(void*, void*, void*);
RestoreFn g_origRestore = nullptr;
bool RestoreHostCars(void* garage, void* edx, void* stored);
bool __fastcall HookRestore(void* garage, void* edx, void* stored) {  // CGarage::RestoreCarsForThisHideOut
    if (!Partner()) {
        if (Net().Online() && Net().IsHost()) return RestoreHostCars(garage, edx, stored);
        return g_origRestore(garage, edx, stored);
    }
    if (core::IsPlayerReady()) {
        Writer w(proto::G_GARAGE);
        w.U8(GARAGE_BRING_OUT);
        w.U8(IndexOf(garage));
        Net().Send(proto::CH_RELIABLE, w);
    }
    return true;  // nothing to wait for here: the door opens (CGarage::Update asks until all cars are out)
}

// The host's own garage restore must leave other players' stored cars in their slots.
// Passing a filtered copy lets GTA restore its own cars without consuming theirs.
bool RestoreHostCars(void* garage, void* edx, void* stored) {
    uint8_t* records = (uint8_t*)stored;
    const std::string host = Net().PeerName(Net().HostId());
    const auto ownersIt = g_storedOwners.find(StoreIndex(garage));
    if (ownersIt == g_storedOwners.end()) return g_origRestore(garage, edx, stored);
    uint8_t filtered[kGarageSlots * kStoredCarSize];
    memcpy(filtered, records, sizeof(filtered));
    for (int i = 0; i < kGarageSlots; ++i) {
        const std::string& owner = ownersIt->second[i];
        if (!owner.empty() && owner != host)
            memset(filtered + i * kStoredCarSize, 0, kStoredCarSize);
    }
    bool done = g_origRestore(garage, edx, filtered);
    for (int i = 0; i < kGarageSlots; ++i) {
        const std::string& owner = ownersIt->second[i];
        if (owner.empty() || owner == host) {
            memcpy(records + i * kStoredCarSize, filtered + i * kStoredCarSize, kStoredCarSize);
            if (!Ref<uint16_t>((uintptr_t)records + (uintptr_t)i * kStoredCarSize + 0x12))
                ownersIt->second[i].clear();
        }
    }
    return done;
}

using CloseFn = void(__cdecl*)();
CloseFn g_origCloseBeforeSave = nullptr;
void __cdecl HookCloseBeforeSave() {  // CGarages::Save
    if (core::SavingSnapshot()) return;  // the partners' copy of the world: an open garage stays open with its cars
    g_origCloseBeforeSave();
}

void Handle(uint8_t from, Reader& r) {
    uint8_t op = r.U8(), index = r.U8();
    void* g = GarageAt(index);
    if (!r.Ok() || from == Net().MyId() || !g || !IsHideout(TypeOf(g)) || !core::IsPlayerReady()) return;
    if (op == GARAGE_STORE && Net().IsHost()) {
        uint8_t n = r.U8();
        if (!r.Ok() || n > kGarageSlots) return;
        uint8_t* store = StoreOf(g);
        std::vector<uint16_t> kept;
        for (int i = 0; i < n && i < 4; ++i) {
            uint16_t id = r.U16();
            NetVehicle* nv = Vehicles::Instance().Find(id);
            if (!r.Ok() || !nv || nv->owner != from || !nv->veh || !IsVehicleValid(nv->veh) || Occupied(nv->veh) ||
                !Inside(g, nv->veh->GetPosition()))
                continue;
            int slotIndex = -1;
            for (int k = 0; k < 4 && slotIndex < 0; ++k)
                if (!Ref<uint16_t>((uintptr_t)store + (uintptr_t)k * kStoredCarSize + 0x12)) slotIndex = k;
            uint8_t* slot = slotIndex >= 0 ? store + slotIndex * kStoredCarSize : nullptr;
            if (!slot) break;  // all four places taken: the car stays where it is
            CallMethod<0x449760>(slot, nv->veh);  // CStoredCar::StoreCar
            g_storedOwners[StoreIndex(g)][slotIndex] = Net().PeerName(from);
            kept.push_back(id);
        }
        if (kept.empty()) return;
        Writer w(proto::G_GARAGE);
        w.U8(GARAGE_KEPT);
        w.U8(index);
        w.U8((uint8_t)kept.size());
        for (uint16_t id : kept) w.U16(id);
        Net().Send(proto::CH_RELIABLE, w);
        LOGI("Garage %s keeps %zu car(s) of player %d", NameOf(g).c_str(), kept.size(), from);
    } else if (op == GARAGE_KEPT && from == Net().HostId() && !Net().IsHost()) {
        uint8_t n = r.U8();
        if (!r.Ok() || n > kGarageSlots) return;
        int gone = 0;
        for (int i = 0; i < n && i < 4; ++i) {
            NetVehicle* nv = Vehicles::Instance().Find(r.U16());
            if (!r.Ok() || !nv || Vehicles::Instance().IsMirror(*nv) || !nv->veh || !IsVehicleValid(nv->veh) || Occupied(nv->veh)) continue;
            Cmd(0x00A6).I(GetVehicleRef(nv->veh)).Run();  // DELETE_CAR: the vehicle sync tells the host's game it is gone
            ++gone;
        }
        LOGI("Garage %s: the host keeps %d car(s) of ours now", NameOf(g).c_str(), gone);
    } else if (op == GARAGE_BRING_OUT && Net().IsHost()) {  // the cars it keeps go to the partner's game: out of our store
        uint8_t* store = StoreOf(g);
        Writer w(proto::G_GARAGE);
        w.U8(GARAGE_CARS_TO);
        w.U8(index);
        w.U8(from);
        std::vector<uint8_t*> cars;
        std::vector<int> slots;
        std::vector<uint16_t> expectedModels;
        const std::string requester = Net().PeerName(from);
        auto& owners = g_storedOwners[StoreIndex(g)];
        for (int k = 0; k < 4; ++k)
            if (uint16_t model = Ref<uint16_t>((uintptr_t)store + (uintptr_t)k * kStoredCarSize + 0x12); model && owners[k] == requester)
                cars.push_back(store + k * kStoredCarSize), slots.push_back(k);
        if (cars.empty()) return;  // never hand another player's garage contents to this requester
        w.U8((uint8_t)cars.size());
        for (size_t i = 0; i < cars.size(); ++i) {
            uint8_t* c = cars[i];
            expectedModels.push_back(Ref<uint16_t>((uintptr_t)c + 0x12));
            w.Bytes(c, kStoredCarSize);
            Ref<uint16_t>((uintptr_t)c + 0x12) = 0;
            owners[slots[i]].clear();
        }
        g_pendingRestore[PendingKey(index, from)] = std::move(expectedModels);
        Net().Send(proto::CH_RELIABLE, w);
        LOGI("Garage %s opened for player %d: its %zu car(s) go out there", NameOf(g).c_str(), from, cars.size());
    } else if ((op == GARAGE_CARS_TO || op == GARAGE_CARS) && from == Net().HostId() && !Net().IsHost()) {
        if (op == GARAGE_CARS_TO && r.U8() != Net().MyId()) return;
        g = GarageAt(index);
        if (!r.Ok() || !g || !IsHideout(TypeOf(g))) return;
        uint8_t n = r.U8(), cars[kGarageSlots * kStoredCarSize] = {};
        if (!r.Ok() || n > kGarageSlots) return;
        for (int k = 0; k < n && k < 4; ++k) {
            std::vector<uint8_t> c = r.Bytes(kStoredCarSize);
            if (c.size() == kStoredCarSize) memcpy(cars + k * kStoredCarSize, c.data(), kStoredCarSize);
        }
        if (!r.Ok()) return;
        // CStoredCar::RestoreCar makes nothing of a model not in memory yet (it only asks for it)
        for (int k = 0; k < 4; ++k)
            if (int model = Ref<uint16_t>((uintptr_t)cars + (uintptr_t)k * kStoredCarSize + 0x12)) LoadModelNow(model);
        g_origRestore(g, nullptr, cars);  // CGarage::RestoreCarsForThisHideOut on the host's records: the cars are ours
        // what could not come out goes back into the host's store
        Writer w(proto::G_GARAGE);
        w.U8(GARAGE_BACK);
        w.U8(index);
        std::vector<uint8_t*> left;
        for (int k = 0; k < 4; ++k)
            if (Ref<uint16_t>((uintptr_t)cars + (uintptr_t)k * kStoredCarSize + 0x12)) left.push_back(cars + k * kStoredCarSize);
        w.U8((uint8_t)left.size());
        for (uint8_t* c : left) w.Bytes(c, kStoredCarSize);
        Net().Send(proto::CH_RELIABLE, w);  // an empty response also closes the host's pending restore
        LOGI("Garage %s: %d car(s) of the host's store out here, %zu back", NameOf(g).c_str(), n - (int)left.size(), left.size());
    } else if (op == GARAGE_BACK && Net().IsHost()) {
        uint8_t n = r.U8();
        if (!r.Ok() || n > kGarageSlots) return;
        const uint16_t pendingKey = PendingKey(index, from);
        auto pending = g_pendingRestore.find(pendingKey);
        if (pending == g_pendingRestore.end() || n > pending->second.size()) return;
        std::vector<uint16_t> remaining = pending->second;
        std::vector<std::vector<uint8_t>> returned;
        for (int k = 0; k < n; ++k) {
            auto c = r.Bytes(kStoredCarSize);
            if (!r.Ok() || c.size() != kStoredCarSize) return;
            uint16_t model = (uint16_t)c[0x12] | ((uint16_t)c[0x13] << 8);
            if (!sync::ConsumeGarageRestoreModel(remaining, model)) return;
            returned.push_back(std::move(c));
        }
        if (!r.Ok()) return;
        uint8_t* store = StoreOf(g);
        for (const auto& c : returned) {
            uint8_t* slot = nullptr;
            for (int s = 0; s < 4 && !slot; ++s)
                if (!Ref<uint16_t>((uintptr_t)store + (uintptr_t)s * kStoredCarSize + 0x12)) slot = store + s * kStoredCarSize;
            if (slot) {
                memcpy(slot, c.data(), kStoredCarSize);
                g_storedOwners[StoreIndex(g)][(slot - store) / kStoredCarSize] = Net().PeerName(from);
            }
        }
        pending->second = std::move(remaining);
        g_pendingRestore.erase(pendingKey);
        LOGI("Garage %s: %zu car(s) back in the store", NameOf(g).c_str(), returned.size());
    }
}

class Garages : public Module {
public:
    const char* Name() const override { return "Garages"; }
    bool OnCommand(const std::string& cmd, const std::vector<std::string>&) override { return false; }
    void OnInstallHooks() override {
        mem::RedirectCallFn(0x44BE44, &HookStoreClosing, &g_origStoreClosing);        // CGarage::Update
        mem::RedirectCallFn(0x44A1EA, &HookStoreBeforeSave, &g_origStoreBeforeSave);  // CGarages::CloseHideOutGaragesBeforeSave
        mem::RedirectCallFn(0x44C1E1, &HookRestore, &g_origRestore);                  // CGarage::Update
        mem::RedirectCallFn(0x5D3165, &HookCloseBeforeSave, &g_origCloseBeforeSave);  // CGarages::Save
        Net().SetHandler(proto::G_GARAGE, &Handle);
    }
};

Garages g_garages;
struct Reg {
    Reg() { core::RegisterModule(&g_garages); }
} g_reg;
}  // namespace

}  // namespace coop
