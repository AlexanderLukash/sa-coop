#include "common/racing/LegacyBlips.h"
#include "common/lib/log/Log.h"
#include "entities/session/Flow.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"

namespace coop {
namespace {
class RaceCleanup : public Module {
    bool m_cleaned = false;
public:
    const char* Name() const override { return "RaceCleanup"; }
    void OnSessionStart() override { m_cleaned = false; }
    void OnSessionEnd() override { m_cleaned = false; }
    void OnGameShutdown() override { m_cleaned = false; }
    void OnGameStarted() override { m_cleaned = false; }
    void OnPostProcess() override {
        if (m_cleaned || !core::IsPlayerReady() || !flow::InSharedWorld() || core::IsOnMission()) return;
        int removed = 0;
        for (int i = 0; i < 175; ++i) {
            uintptr_t trace = 0xBA86F0 + uintptr_t(i) * 0x28;
            auto position = game::Ref<game::CVector>(trace + 0x08);
            if (street::LegacyRaceBlip((game::Ref<uint8_t>(trace + 0x25) & 2) != 0,
                    (game::Ref<uint8_t>(trace + 0x26) >> 2) & 0xF,
                    game::Ref<uint8_t>(trace + 0x24), game::Ref<uint32_t>(trace),
                    {position.x, position.y, position.z})) {
                game::Call<0x587C10>(i); // CRadar::ClearActualBlip
                ++removed;
            }
        }
        LOGI("Removed %d saved street-race blips", removed);
        m_cleaned = true;
    }
};
struct Reg { Reg() { static RaceCleanup module; core::RegisterModule(&module); } } g_reg;
}
}
