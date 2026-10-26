#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "common/lib/buffer/Buffer.h"

namespace coop::street {
enum class VehicleClass : uint8_t { None, Street, Tuned, Muscle, Sports, OffRoad, Performance };
constexpr uint16_t ClassBit(VehicleClass c) { return c == VehicleClass::None ? 0 : uint16_t(1u << unsigned(c)); }
enum class Region : uint8_t { LosSantos, RedCounty, SanFierro, LasVenturas };
struct Route {
    uint16_t id;
    const char* name;
    Region region;
    uint8_t cityLevel, difficulty, minimum, maximum;
    uint16_t classes;
    uint32_t minimumStake, maximumStake;
    std::vector<Vec3> checkpoints;  // first point is the grid origin, last is the finish
    float Length() const;
};
const std::vector<Route>& Routes();
const Route* FindRoute(uint16_t id);
VehicleClass ClassOf(uint16_t model);
float MaximumSpeed(VehicleClass vehicleClass);  // generous stock/upgraded ceiling, metres per second
bool Eligible(const Route& route, uint16_t model);
bool Available(const Route& route, bool cesarPassed, uint8_t cityLevel);
std::string AllowedClasses(const Route& route);
float Distance(Vec3 a, Vec3 b);
float DistanceToSegment(Vec3 point, Vec3 a, Vec3 b);
Vec3 GridSpot(const Route& route, unsigned slot);
}
