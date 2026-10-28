#pragma once
#include <string_view>

namespace coop::sync {

// Script names occupy eight bytes and the engine may normalize their case.
inline bool MissionNameEquals(std::string_view name, std::string_view expected) {
    const auto zero = name.find('\0');
    if (zero != std::string_view::npos) name = name.substr(0, zero);
    if (name.size() != expected.size()) return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != expected[i]) return false;
    }
    return true;
}

inline bool BlockLocalStoryMission(bool online, bool host, bool mission, bool sideMission,
                                   bool sideLauncher, bool ownIntro, std::string_view name) {
    if (!online || host || !mission || sideMission || sideLauncher) return false;
    // INITIAL/INITIL2 build the world when loading a new game. BUYPRO1 is a
    // synchronized property purchase, not a story mission.
    if (MissionNameEquals(name, "initial") || MissionNameEquals(name, "initil2") ||
        MissionNameEquals(name, "buypro1")) return false;
    if (ownIntro && MissionNameEquals(name, "intro")) return false;
    return true;
}

}  // namespace coop::sync
