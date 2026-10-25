#pragma once
#include "common/lib/buffer/Buffer.h"

namespace coop::sync {
// This fork's v4 stored homes where upstream v4/v5 stored fighting moves.
// Version 6 extends our v4; upstream v5 is deliberately not misread as a fork profile.
constexpr uint16_t kProfileVersion = 6;
inline bool ForkProfileVersion(uint16_t version) {
    return (version >= 1 && version <= 4) || version == kProfileVersion;
}
inline bool ReadProfileExtension(Reader& r, uint16_t version, uint32_t& homes, uint8_t& moves) {
    if (!ForkProfileVersion(version)) return false;
    homes = version >= 4 ? r.U32() : 0;
    moves = version >= 6 ? r.U8() : 0;
    return r.Ok();
}
inline void WriteProfileExtension(Writer& w, uint32_t homes, uint8_t moves) {
    w.U32(homes);
    w.U8(moves);
}
}  // namespace coop::sync
