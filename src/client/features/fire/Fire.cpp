// Fires both games share. Fires on the ground from explosions and molotovs start in each game by themselves (those
// are synced), so only these travel: a fire on a player, a ped or a vehicle this game owns (shot with a molotov,
// a flamethrower, a burning car), and the host's mission fires (START_SCRIPT_FIRE). The other game lights the same
// fire on its copy of the entity, or at the spot: such a fire never spreads, never burns the copy (copies are
// fireproof, the proof is lifted only to light it) and goes out when the owner's fire does. A copy this game's player
// puts out (water, an extinguisher) puts the owner's fire out too: the partner helps with the host's firefighting.
#include <map>

#include "common/api/protocol/Protocol.h"
#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "entities/mission/Mission.h"
#include "entities/ped/Peds.h"
#include "entities/player/Players.h"
#include "entities/vehicle/Vehicles.h"
#include "shared/api/net-session/Session.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"

namespace coop {

namespace {
using namespace game;

constexpr uintptr_t kFireManager = 0xB71F80;  // gFireManager: CFire m_aFires[60], 0x28 bytes each
constexpr int kFires = 60;
constexpr uint32_t kFireProof = 1u << 19;     // CPhysical::physicalFlags.bFireProof
enum : uint8_t { FIRE_START = 1, FIRE_STOP = 2, FIRE_OUT = 3 };  // OUT: a copy was put out, to the owner
enum : uint8_t { ON_SPOT = 0, ON_VEHICLE = 1, ON_PED = 2, ON_PLAYER = 3 };

uintptr_t Fire(int i) { return kFireManager + (uintptr_t)i * 0x28; }
bool Active(uintptr_t f) { return Ref<uint8_t>(f) & 1; }
CEntity* Target(uintptr_t f) { return Ref<CEntity*>(f + 0x10); }

struct Sent {
    uint16_t id;
    CEntity* target;
};
std::map<uintptr_t, Sent> g_sent;         // our fires the other game copied
struct Copy {
    uintptr_t fire;
    uint8_t from;  // whose fire it copies
};
std::map<uint16_t, Copy> g_copies;    // fires lit here for the other game, by its id
uint16_t g_nextId = 1;
uint64_t g_scanMs = 0;

bool IsCopy(uintptr_t f) {
    for (auto& [id, c] : g_copies)
        if (c.fire == f) return true;
    return false;
}

// this game's player could have put it out: close, with water or an extinguisher (a fire truck's cannon, weapon 42)
bool PlayerFighting(const CVector& at) {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || (me->GetPosition() - at).Length() > 30.0f) return false;
    if (me->InVehicle()) return me->Vehicle() && me->Vehicle()->ModelIndex() == 407;  // FIRETRUK
    return me->ActiveWeapon().type == 42;
}

// what the other game knows this burning entity as; false when it does not know it
bool Describe(CEntity* e, uint8_t& kind, uint16_t& id) {
    if (!e) return false;
    if (e->IsPed()) {
        if (e == (CEntity*)FindPlayerPed(0)) return kind = ON_PLAYER, id = Net().MyId(), true;
        NetPed* np = Peds::Instance().Find((CPed*)e);
        return np && np->owner == Net().MyId() && (kind = ON_PED, id = np->netId, true);
    }
    if (e->Type() == ENTITY_TYPE_VEHICLE) {
        NetVehicle* nv = Vehicles::Instance().Find((CVehicle*)e);
        return nv && !Vehicles::Instance().IsMirror(*nv) && (kind = ON_VEHICLE, id = nv->netId, true);
    }
    return false;
}

// the copy of what burns in the other game
CEntity* Resolve(uint8_t kind, uint16_t id) {
    if (kind == ON_VEHICLE) {
        NetVehicle* nv = Vehicles::Instance().Find(id);
        return nv && nv->veh && IsVehicleValid(nv->veh) ? (CEntity*)nv->veh : nullptr;
    }
    if (kind == ON_PED) return (CEntity*)GetPed(Peds::Instance().HandleOf(id));
    if (kind == ON_PLAYER) {
        RemotePlayer* rp = Players::Instance().Get((uint8_t)id);
        return rp ? (CEntity*)rp->ped : nullptr;
    }
    return nullptr;
}

void SendStop(uint16_t id) {
    Writer w(proto::G_FIRE);
    w.U8(FIRE_STOP);
    w.U16(id);
    Net().Send(proto::CH_RELIABLE, w);
}

void Scan() {
    for (int i = 0; i < kFires; ++i) {
        uintptr_t f = Fire(i);
        auto sent = g_sent.find(f);
        if (!Active(f) || IsCopy(f)) {
            if (sent != g_sent.end()) {
                SendStop(sent->second.id);
                g_sent.erase(sent);
            }
            continue;
        }
        CEntity* target = Target(f);
        if (sent != g_sent.end()) {
            if (sent->second.target == target) continue;
            SendStop(sent->second.id);  // the slot holds another fire now
            g_sent.erase(sent);
        }
        uint8_t kind = ON_SPOT;
        uint16_t entityId = 0;
        bool script = Ref<uint8_t>(f) & 2;  // bCreatedByScript
        if (target ? !Describe(target, kind, entityId) : !(script && Net().IsHost() && HostReplicating())) continue;
        uint16_t id = g_nextId++;
        g_sent[f] = {id, target};
        CVector pos = Ref<CVector>(f + 0x04);
        Writer w(proto::G_FIRE);
        w.U8(FIRE_START);
        w.U16(id);
        w.U8(kind);
        w.U16(entityId);
        w.Vec({pos.x, pos.y, pos.z});
        w.F32(Ref<float>(f + 0x1C));  // m_fStrength
        Net().Send(proto::CH_RELIABLE, w);
        LOGD("Fire %04X sent (on %d %04X)", id, kind, entityId);
    }
}

void Light(uint8_t from, uint16_t id, uint8_t kind, uint16_t entityId, const CVector& pos, float strength) {
    uintptr_t fire = 0;
    if (kind == ON_SPOT) {
        // CFireManager::StartFire(CVector, size, unused, creator, lifetime, generations, unused): no spreading
        fire = CallMethod<0x539F00, uintptr_t>((void*)kFireManager, pos.x, pos.y, pos.z, strength, (uint8_t)1,
                                                (CEntity*)nullptr, (uint32_t)600000, (int8_t)0, (uint8_t)1);
        if (fire && strength > 1.0f) {  // it lights every fire at strength 1 (CFire::Start): a mission's big fire too
            Ref<float>(fire + 0x1C) = strength;
            CVector at = Ref<CVector>(fire + 0x04);
            CallMethod<0x539360>((void*)fire, &at, (void*)nullptr);  // CFire::CreateFxSysForStrength
        }
    } else {
        CEntity* e = Resolve(kind, entityId);
        if (!e) return;
        auto* p = (CPhysical*)e;
        uint32_t flags = p->PhysicalFlags();
        p->PhysicalFlags() = flags & ~kFireProof;
        // CFireManager::StartFire(target, creator, size, unused, lifetime, generations)
        CallMethod<0x53A050, void*>((void*)kFireManager, e, (CEntity*)nullptr, 0.8f, (uint8_t)1, (uint32_t)600000, (int8_t)0);
        p->PhysicalFlags() = flags;
        fire = Field<uintptr_t>(e, e->IsPed() ? 0x730 : 0x490);  // CPed::m_pFire, CVehicle::m_pFire
    }
    if (!fire) return;
    g_copies[id] = {fire, from};
    LOGD("Fire %04X lit (on %d %04X)", id, kind, entityId);
}

void PutOut(uint16_t id) {
    auto it = g_copies.find(id);
    if (it == g_copies.end()) return;
    if (Active(it->second.fire)) CallMethod<0x5393F0>((void*)it->second.fire);  // CFire::Extinguish
    g_copies.erase(it);
}

// the owner's side of FIRE_OUT: its fire goes out as the copy did
void PutOutOwn(uint16_t id) {
    for (auto it = g_sent.begin(); it != g_sent.end(); ++it)
        if (it->second.id == id) {
            if (Active(it->first)) CallMethod<0x5393F0>((void*)it->first);  // CFire::Extinguish
            g_sent.erase(it);
            LOGI("Fire %04X put out by the other player", id);
            return;
        }
}

class Fires : public Module {
public:
    const char* Name() const override { return "Fires"; }

    void OnInstallHooks() override {
        Net().SetHandler(proto::G_FIRE, [](uint8_t from, Reader& r) {
            uint8_t op = r.U8();
            uint16_t id = r.U16();
            if (!r.Ok() || !core::IsPlayerReady()) return;
            if (op == FIRE_STOP) return PutOut(id);
            if (op == FIRE_OUT) return PutOutOwn(id);
            uint8_t kind = r.U8();
            uint16_t entityId = r.U16();
            Vec3 p = r.Vec();
            float strength = r.F32();
            if (!r.Ok()) return;
            PutOut(id);
            Light(from, id, kind, entityId, {p.x, p.y, p.z}, strength);
        });
    }
    void OnSessionEnd() override { Forget(); }
    void OnGameStarted() override { Forget(); }
    void OnPeerResync(uint8_t) override { g_sent.clear(); }  // sent again by the next scan

    void OnPostProcess() override {
        if (!Net().Online() || !core::IsPlayerReady() || NowMs() - g_scanMs < 200) return;
        g_scanMs = NowMs();
        // copies whose fire went out here by itself (water, the entity died) stay out; put out by this player, the
        // owner's goes out too
        for (auto it = g_copies.begin(); it != g_copies.end();) {
            if (Active(it->second.fire)) {
                ++it;
                continue;
            }
            if (PlayerFighting(Ref<CVector>(it->second.fire + 0x04))) {
                Writer w(proto::G_FIRE);
                w.U8(FIRE_OUT);
                w.U16(it->first);
                Net().Send(proto::CH_RELIABLE, w);
                LOGI("Fire %04X of player %d put out here", it->first, it->second.from);
            }
            it = g_copies.erase(it);
        }
        Scan();
    }

    static Fires& Instance() {
        static Fires m;
        return m;
    }

private:
    static void Forget() {
        g_sent.clear();
        g_copies.clear();
    }
};

struct Reg {
    Reg() { core::RegisterModule(&Fires::Instance()); }
} g_reg;
}  // namespace

}  // namespace coop
