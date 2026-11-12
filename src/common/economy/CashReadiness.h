#pragma once
#include <cstdint>

namespace coop::economy {
// A ledger snapshot may arrive before the host identifies its loaded save, or
// before a partner's profile is applied to a newly loaded world. Neither is a
// reason to overwrite the game's native cash yet.
struct CashReadiness {
    uint32_t game = 0, expectedHostGame = 0;
    bool gameKnown = false, hostExpected = false, profileApplied = false;

    void BeginWorld() {
        hostExpected = false;
        profileApplied = false;
    }
    void ExpectHostGame(uint32_t id) {
        expectedHostGame = id;
        hostExpected = true;
    }
    bool ReceiveGame(uint32_t id) {
        bool changed = !gameKnown || game != id;
        gameKnown = true;
        game = id;
        if (changed) profileApplied = false;
        return changed;
    }
    bool Ready(bool host) const {
        return gameKnown && (host ? hostExpected && expectedHostGame == game : profileApplied);
    }
};
}  // namespace coop::economy
