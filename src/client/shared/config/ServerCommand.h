#pragma once
#include "shared/config/Config.h"
#include "common/lib/util/Util.h"

namespace coop {
// Windows CommandLineToArgvW quoting, including embedded quotes and trailing slashes.
inline std::wstring QuoteProcessArgument(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"') out += L'\\';
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
inline std::wstring BuildServerCommand(const ClientConfig& cfg, uint16_t port,
                                       const std::string& password, const std::wstring& world, bool newGame) {
    std::wstring command = QuoteProcessArgument(cfg.coopDir + L"coop-server.exe") +
        L" --port " + std::to_wstring(port) + L" --data " +
        QuoteProcessArgument(cfg.coopDir + Utf8ToWide(cfg.serverData)) + L" --name " +
        QuoteProcessArgument(Utf8ToWide(cfg.serverName)) + L" --host " + QuoteProcessArgument(Utf8ToWide(cfg.nickname));
    if (!password.empty()) command += L" --password " + QuoteProcessArgument(Utf8ToWide(password));
    if (!world.empty()) command += L" --world " + QuoteProcessArgument(world);
    if (newGame) command += L" --new-game";
    return command + (cfg.friendlyFire ? L" --friendly-fire 1" : L" --friendly-fire 0");
}
} // namespace coop
