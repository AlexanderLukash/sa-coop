#pragma once
#include "shared/game/Game.h"

namespace coop::appearance {
void InstallHooks();
int LogicalSkin(int model);
int ResolveSkin(int id);
bool ApplySkin(game::CPlayerPed* ped, int skin);
}  // namespace coop::appearance
