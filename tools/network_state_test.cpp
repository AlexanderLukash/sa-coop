// Actual client wire formats, exercised without accessing the GTA process.
#include <cassert>
#include <cmath>
#include <limits>
#include "entities/player/PlayerState.h"
#include "entities/vehicle/VehicleState.h"
using namespace coop;

template<class T> T RoundTrip(const T& input) {
    Writer w;
    input.Write(w);
    Reader r(w.Data(), w.Size());
    T output;
    assert(output.Read(r));
    assert(r.Ok() && r.Remaining() == 0);
    // Every truncated variant must fail rather than supply defaults as a valid state.
    for (size_t n = 0; n < w.Size(); ++n) {
        Reader shortPacket(w.Data(), n);
        T rejected;
        assert(!rejected.Read(shortPacket));
    }
    return output;
}
int main() {
    OnFootState player;
    player.pos = {123, -456, 1000};
    player.flags = OnFootState::F_PHONE | OnFootState::F_TASK_WALK;
    player.flags2 = OnFootState::F2_DOOR;
    player.area = 3;
    player.weapon = 24;
    player.pad.buttons = PadState::B_TRIANGLE;
    auto received = RoundTrip(player);
    assert(received.flags == player.flags && received.flags2 == player.flags2);
    assert(received.area == 3 && received.weapon == 24 && received.pad.buttons == player.pad.buttons);
    for (uint8_t extras = 0; extras < 64; ++extras) {
        VehicleState car;
        car.pos = {11, 22, 33};
        car.flags = VehicleState::F_ENGINE | VehicleState::F_HANDBRAKE | VehicleState::F_HORN;
        car.extras = extras;
        car.misc = 123;
        car.gear = 0.5f;
        car.turretYaw = 1;
        car.lightTarget = {50, 60, 70};
        car.lightPower = 0.8f;
        car.tower = 0x8001;  // eighth player's entity namespace
        car.lean = 0.4f;
        car.leanFwd = -0.6f;
        auto copy = RoundTrip(car);
        assert(copy.flags == car.flags && copy.extras == extras);
        if (extras & VehicleState::X_LIGHT) assert(copy.lightTarget.z == 70 && std::fabs(copy.lightPower - 0.8f) < 0.01f);
        if (extras & VehicleState::X_TOW) assert(copy.tower == 0x8001);
        if (extras & VehicleState::X_BIKE) assert(std::fabs(copy.leanFwd + 0.6f) < 0.001f);
    }
    player.pos.x = std::numeric_limits<float>::quiet_NaN();
    Writer invalid;
    player.Write(invalid);
    Reader r(invalid.Data(), invalid.Size());
    assert(!received.Read(r));
}
