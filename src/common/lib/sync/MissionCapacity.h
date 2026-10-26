#pragma once

#include <cstddef>
#include <cstdint>

namespace coop::sync {

// Mission rendezvous must use a recent player snapshot. A stale ped may still be
// rendered at the marker while its owner is loading, disconnected from gameplay,
// or stalled, which would let the host start without that participant.
constexpr bool MissionParticipantReady(bool hasPed, bool hasState, uint64_t stateAgeMs, bool sameArea, bool atMarker,
                                      uint64_t maxStateAgeMs = 3000) {
    return hasPed && hasState && stateAgeMs <= maxStateAgeMs && sameArea && atMarker;
}

// A majority quorum rounds up: one of two players and two of three qualify.
constexpr bool MissionQuorumReached(std::size_t activePlayers, std::size_t atMarker) {
    return activePlayers != 0 && atMarker >= (activePlayers + 1) / 2;
}

// The session peer list contains remote players only; the local host is additional.
constexpr std::size_t ActivePlayersFromPeers(std::size_t peerCount) { return peerCount + 1; }

// NPCs occupy seats only in the original vehicle; support vehicles start empty.
// Player-occupied seats remain part of the party's capacity.
constexpr std::size_t RequiredMissionVehicles(std::size_t activePlayers, std::size_t passengerSeats,
                                              std::size_t npcSeats = 0) {
    if (activePlayers == 0) return 0;
    const std::size_t capacity = passengerSeats + 1;
    const std::size_t available = capacity - (npcSeats < capacity ? npcSeats : capacity);
    if (activePlayers <= available) return 1;
    return 1 + (activePlayers - available + capacity - 1) / capacity;
}

}  // namespace coop::sync
