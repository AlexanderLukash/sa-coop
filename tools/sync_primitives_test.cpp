#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "common/api/protocol/Protocol.h"
#include "common/lib/buffer/Buffer.h"
#include "common/lib/sync/StateTiming.h"
#include "common/lib/sync/RemovalHistory.h"
#include "common/lib/sync/MissionCapacity.h"
#include "common/lib/sync/GarageRestore.h"
#include "common/lib/sync/ProfileExtension.h"

using namespace coop;

int main() {
    for (bool allowed : {false, true}) {
        proto::Welcome original;
        original.allowCheats = allowed;
        Writer packet;
        original.Write(packet);
        proto::Welcome received;
        Reader input(packet.Data(), packet.Size());
        assert(received.Read(input) && input.Remaining() == 0 && received.allowCheats == allowed);
        Reader truncated(packet.Data(), packet.Size() - 1);
        assert(!received.Read(truncated));
    }
    for (int32_t amount : {0, 123456, proto::BalanceChange::kMaximum, -1, 1000000000}) {
        proto::BalanceChange original{7, amount}, received;
        Writer packet;
        original.Write(packet);
        Reader input(packet.Data(), packet.Size());
        assert(received.Read(input) == (amount >= 0 && amount <= proto::BalanceChange::kMaximum));
        for (size_t length = 0; length < packet.Size(); ++length) {
            Reader truncated(packet.Data(), length);
            assert(!received.Read(truncated));
        }
    }
    {
        Writer packet;
        proto::BalanceChange{8, 100}.Write(packet);
        Reader invalidTarget(packet.Data(), packet.Size());
        proto::BalanceChange received;
        assert(!received.Read(invalidTarget));
        packet.Buffer()[0] = 0;
        packet.U8(0);
        Reader trailing(packet.Data(), packet.Size());
        assert(!received.Read(trailing));
    }
    // A pre-update fork profile preserves its home owners and has no moves byte.
    Writer legacyProfile;
    legacyProfile.U32(0x80000008u);
    Reader legacy(legacyProfile.Data(), legacyProfile.Size());
    uint32_t homes = 0;
    uint8_t moves = 255;
    assert(sync::ReadProfileExtension(legacy, 4, homes, moves));
    assert(homes == 0x80000008u && moves == 0 && legacy.Remaining() == 0);
    // New profiles keep homes, moves and the following side-progress bytes separate.
    Writer profile;
    sync::WriteProfileExtension(profile, homes, 7);
    profile.U32(0x12345678);
    Reader current(profile.Data(), profile.Size());
    assert(sync::ReadProfileExtension(current, 6, homes, moves));
    assert(homes == 0x80000008u && moves == 7 && current.U32() == 0x12345678);
    Reader incompatible(profile.Data(), profile.Size());
    assert(!sync::ReadProfileExtension(incompatible, 5, homes, moves));
    Reader truncated(profile.Data(), 4);
    assert(!sync::ReadProfileExtension(truncated, 6, homes, moves));

    // A reliable TRAIN_GONE can overtake an older state on the other channel.
    sync::RemovalHistory removed;
    constexpr uint32_t trainA = (1u << 16) | 7, trainB = (2u << 16) | 7;
    assert(!removed.Contains(trainA, 100));
    removed.Remember(trainA, 100);
    assert(removed.Contains(trainA, 101));
    assert(!removed.Contains(trainB, 101));  // IDs are scoped to their sender
    assert(removed.Contains(trainA, 10100));
    assert(!removed.Contains(trainA, 10101));  // reuse allowed after the retention period
    removed.Expire(10101);
    removed.Remember(trainA, 11000);
    removed.Remember(trainB, 11000);
    removed.ForgetOwner(1);  // peer restarted; its ID namespace starts over
    assert(!removed.Contains(trainA, 11001));
    assert(removed.Contains(trainB, 11001));
    removed.Clear();
    assert(!removed.Contains(trainB, 11001));

    // Lost train packets must not project motion for the entire outage.
    float previous = -1;
    for (uint64_t age : {0ull, 100ull, 300ull, 500ull, 1500ull, 9000ull}) {
        float distance = 2.0f * sync::ExtrapolationSeconds(age, 300) * 50.0f;
        assert(distance >= previous && distance <= 30.001f);
        previous = distance;
    }
    assert(sync::Freshness(1500, 500, 1000) == 0.0f);

    std::vector<uint16_t> expectedGarageModels{411, 451, 411};
    assert(sync::ConsumeGarageRestoreModel(expectedGarageModels, 411));
    assert(sync::ConsumeGarageRestoreModel(expectedGarageModels, 411));
    assert(!sync::ConsumeGarageRestoreModel(expectedGarageModels, 411));  // duplicate/replay
    assert(!sync::ConsumeGarageRestoreModel(expectedGarageModels, 999));  // wasn't sent
    assert(!sync::ConsumeGarageRestoreModel(expectedGarageModels, 0));
    assert(sync::ConsumeGarageRestoreModel(expectedGarageModels, 451));
    assert(expectedGarageModels.empty());

    assert(sync::RequiredMissionVehicles(0, 3) == 0);
    assert(sync::ActivePlayersFromPeers(0) == 1);
    assert(sync::ActivePlayersFromPeers(1) == 2);
    assert(sync::RequiredMissionVehicles(2, 3) == 1);  // four seats total
    assert(sync::RequiredMissionVehicles(2, 0) == 2);  // two players, driver-only car
    assert(sync::RequiredMissionVehicles(2, 1) == 1);  // two seats total
    assert(sync::RequiredMissionVehicles(5, 3) == 2);
    assert(sync::RequiredMissionVehicles(8, 0) == 8);  // driver-only vehicles
    assert(sync::RequiredMissionVehicles(2, 3, 3) == 2);  // Drive-Thru: gang leaves one player seat
    assert(sync::RequiredMissionVehicles(2, 3, 2) == 1);  // both players fit
    assert(sync::RequiredMissionVehicles(3, 3, 2) == 2);  // one player needs transport
    assert(sync::RequiredMissionVehicles(8, 3, 3) == 3);  // seven players share two empty cars
    assert(sync::RequiredMissionVehicles(2, 1, 1) == 2);  // NPC driver, host passenger
    assert(sync::RequiredMissionVehicles(0, 3, 3) == 0);
    assert(sync::MissionParticipantReady(true, true, 100, true, true));
    assert(!sync::MissionParticipantReady(true, false, 100, true, true));
    assert(!sync::MissionParticipantReady(true, true, 3001, true, true));
    assert(!sync::MissionParticipantReady(true, true, 100, false, true));
    assert(!sync::MissionParticipantReady(true, true, 100, true, false));
    assert(sync::MissionQuorumReached(1, 1));
    assert(sync::MissionQuorumReached(2, 1));
    assert(!sync::MissionQuorumReached(3, 1));
    assert(sync::MissionQuorumReached(3, 2));
    assert(!sync::MissionQuorumReached(0, 0));
    assert(sync::IsNewer(12, 11));
    assert(sync::IsNewer(0, UINT32_MAX));  // sender clock wrapped
    assert(!sync::IsNewer(11, 12));
    assert(proto::IsValidEntityId(0x1000));
    assert(proto::IsValidEntityId(0x8000));
    assert(!proto::IsValidEntityId(0x0001));
    assert(!proto::IsValidEntityId(0x9000));
    assert(sync::AgeMs(150, 100) == 50);
    assert(sync::AgeMs(99, 100) == 0);
    assert(sync::ExtrapolationSeconds(800, 250) == 0.25f);
    assert(sync::Freshness(400, 500, 1000) == 1.0f);
    assert(sync::Freshness(1000, 500, 1000) == 0.5f);
    assert(sync::Freshness(1500, 500, 1000) == 0.0f);
    assert(sync::PlausiblePosition({-3000, 3000, 1000}));
    assert(!sync::PlausiblePosition({10001, 0, 0}));
    assert(sync::PlausibleVelocity({3, 4, 0}, 5));
    assert(!sync::PlausibleVelocity({3, 4, 1}, 5));

    const uint8_t truth[] = {1};
    Reader goodBool(truth, sizeof truth);
    assert(goodBool.Bool() && goodBool.Ok());
    const uint8_t invalidBool[] = {2};
    Reader badBool(invalidBool, sizeof invalidBool);
    (void)badBool.Bool();
    assert(!badBool.Ok());

    const uint8_t infinity[] = {0, 0, 0x80, 0x7F};
    Reader badFloat(infinity, sizeof infinity);
    assert(badFloat.F32() == 0.0f && !badFloat.Ok());

    const uint8_t overflowingVarint[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x10};
    Reader badVarint(overflowingVarint, sizeof overflowingVarint);
    (void)badVarint.VarU32();
    assert(!badVarint.Ok());

    Reader nullEmpty(nullptr, 0);
    assert(nullEmpty.Ok() && nullEmpty.Remaining() == 0);
    Reader nullNonEmpty(nullptr, 1);
    assert(!nullNonEmpty.Ok() && nullNonEmpty.Remaining() == 0);

    Writer safeQuant;
    safeQuant.I16(Writer::Quant(std::numeric_limits<float>::infinity(), 1.0f));
    safeQuant.Raw(nullptr, 0);
    safeQuant.Raw(nullptr, 4);
    assert(safeQuant.Size() == 2);
    return 0;
}
