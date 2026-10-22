#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

#include "common/api/protocol/Protocol.h"
#include "common/lib/buffer/Buffer.h"
#include "common/lib/sync/StateTiming.h"

using namespace coop;

int main() {
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
