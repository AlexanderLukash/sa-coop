#pragma once
#include <cstdint>

#include "common/lib/buffer/Buffer.h"
#include "shared/game/Game.h"

namespace coop {

// Orientation as a quaternion (compressed to 4 x int16 on the wire).
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    static Quat FromMatrix(const game::CMatrix& m);
    void ToMatrix(game::CMatrix& m) const;  // writes right/forward/up, keeps pos
    static Quat Slerp(const Quat& a, const Quat& b, float t);
    void Write(Writer& w) const;
    void Read(Reader& r);
};

struct VehicleState {
    uint32_t time = 0;
    game::CVector pos;
    Quat rot;
    game::CVector vel;   // m/s
    game::CVector turn;  // rad/s
    float steer = 0;     // radians
    float gas = 0;       // -1..1
    float brake = 0;     // 0..1
    float health = 1000;
    uint8_t flags = 0;   // F_*

    enum : uint8_t { F_ENGINE = 1, F_LIGHTS = 2, F_SIREN = 4, F_HORN = 8, F_HANDBRAKE = 16, F_WRECKED = 32 };
    // parts the driver moves; X_* says which this vehicle has
    uint8_t extras = 0;
    uint16_t misc = 0;  // X_MISC, CAutomobile::m_wMiscComponentAngle: Hydra nozzles, raised hydraulics, tow hoist, forks
    float gear = 0;     // X_GEAR, CPlane::m_fLandingGearStatus: 0 down, 1 up, negative while going down
    float turretYaw = 0, turretPitch = 0;  // X_TURRET: Rhino, SWAT van, fire truck
    enum : uint8_t { X_MISC = 1, X_GEAR = 2, X_TURRET = 4 };

    static VehicleState Capture(game::CVehicle* v);
    void Write(Writer& w) const;
    bool Read(Reader& r);
};

// Static description used to create the vehicle on the other side.
struct VehicleSpawn {
    uint16_t netId = 0;
    uint16_t model = 0;
    uint8_t owner = 0;
    game::CVector pos;
    Quat rot;
    uint8_t colors[4] = {};
    float health = 1000;
    int8_t area = 0;
    uint8_t lockState = 0;
    bool script = false;  // a script's car in its owner's game (a mission car): never an ordinary car elsewhere

    void Write(Writer& w) const;
    bool Read(Reader& r);
};

}  // namespace coop
