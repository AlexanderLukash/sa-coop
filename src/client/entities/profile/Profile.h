#pragma once
#include <cstdint>
// The partner's profile on the host's server: money, skills and look of its own. The progress feature
// (features/progress) keeps it and answers this.

namespace coop {

// partner: send the profile (money, skills, look) now and keep it for the next world load
void SaveProfileNow();

// Personal safe houses belong to the partner profile, not to the host's shared save.
uint32_t PersonalPropertyMask();
void SetPersonalPropertyMask(uint32_t mask);

}  // namespace coop
