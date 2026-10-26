#pragma once
#include <cstdint>
#include <vector>
namespace coop {
bool JoinStreetRace(uint64_t event);
bool LeaveStreetRace(uint64_t event);
bool CreateStreetRace(uint16_t route,uint32_t stake,uint8_t capacity,const std::vector<uint8_t>& invited);
bool CreateStreetTestRace();
bool CancelStreetRace(uint64_t event);
void PreviewStreetRoute(uint64_t event);
}
