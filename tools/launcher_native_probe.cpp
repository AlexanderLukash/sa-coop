// Integration fixture: real ClientConfig/INI/command builder + real ENet server.
// It does not emulate GTA rendering, scripts, saves or character models.
#include <enet/enet.h>
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include "shared/config/Config.h"
#include "shared/config/ServerCommand.h"
#include "common/api/protocol/Protocol.h"
#include "common/config/version/Version.h"
#include "common/lib/ini/Ini.h"
#include "common/lib/util/Util.h"

using namespace coop;
std::string Json(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '\\' || c == '"') out += '\\';
        if (c < 32) { out += ' '; continue; }
        out += (char)c;
    }
    return out + '"';
}
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::string Dump(const ClientConfig& c) {
    auto command = BuildServerCommand(c, (uint16_t)c.port, c.password, L"", c.loadSlot < 0);
    int count = 0;
    wchar_t** args = CommandLineToArgvW(command.c_str(), &count);
    std::string arguments = "[";
    for (int i = 0; i < count; ++i) { if (i) arguments += ','; arguments += Json(WideToUtf8(args[i])); }
    LocalFree(args);
    arguments += ']';
    return "{\"nickname\":" + Json(c.nickname) + ",\"skin\":" + std::to_string(c.skin) +
        ",\"serverName\":" + Json(c.serverName) + ",\"password\":" + Json(c.password) +
        ",\"host\":" + Json(c.host) + ",\"port\":" + std::to_string(c.port) +
        ",\"hostMode\":" + (c.wantsHost ? "true" : "false") + ",\"autoConnect\":" + (c.autoConnect ? "true" : "false") +
        ",\"windowed\":" + (c.windowed ? "true" : "false") + ",\"width\":" + std::to_string(c.width) +
        ",\"height\":" + std::to_string(c.height) + ",\"voice\":" + (c.voiceEnabled ? "true" : "false") +
        ",\"volume\":" + std::to_string(c.voiceVolume) + ",\"micGain\":" + std::to_string(c.micGain) +
        ",\"loadSlot\":" + std::to_string(c.loadSlot) + ",\"serverArgs\":" + arguments + "}";
}
struct ServerProcess {
    HANDLE job = nullptr, process = nullptr;
    ~ServerProcess() { if (job) CloseHandle(job); if (process) { WaitForSingleObject(process, 3000); CloseHandle(process); } }
    void Start(const ClientConfig& cfg) {
        std::wstring command = BuildServerCommand(cfg, (uint16_t)cfg.port, cfg.password, L"", cfg.loadSlot < 0);
        STARTUPINFOW start{sizeof(start)}; PROCESS_INFORMATION info{};
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        Check(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof(limit)), "job setup failed");
        Check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                             nullptr, cfg.coopDir.c_str(), &start, &info), "server launch failed");
        process = info.hProcess;
        Check(AssignProcessToJobObject(job, process), "job assignment failed");
        ResumeThread(info.hThread); CloseHandle(info.hThread);
    }
};
int wmain(int argc, wchar_t** argv) {
    auto& cfg = Cfg();
    try {
        const bool inspect = argc == 3 && std::wstring(argv[1]) == L"--inspect";
        std::wstring coopDir = inspect ? std::wstring(argv[2]) : ModuleDir() + L"coop\\";
        if (coopDir.back() != L'\\') coopDir += L'\\';
        Check(cfg.Load(coopDir), "config load failed");
        if (inspect) {
            Ini before, after;
            Check(before.Load(cfg.iniPath), "INI load failed");
            Check(before.Save(cfg.iniPath + L".roundtrip"), "INI save failed");
            Check(after.Load(cfg.iniPath + L".roundtrip"), "INI roundtrip load failed");
            Check(after.Get("connect", "password") == cfg.password, "quoted password changed during native INI save");
            std::puts(Dump(cfg).c_str()); return 0;
        }
        cfg.WriteLauncherStatus("loaded");
        ServerProcess server;
        if (cfg.wantsHost) server.Start(cfg);
        Check(enet_initialize() == 0, "enet init failed");
        ENetHost* host = enet_host_create(nullptr, 1, proto::CH_COUNT, 0, 0);
        Check(host != nullptr, "enet host failed");
        enet_host_compress_with_range_coder(host);
        ENetAddress address{};
        enet_address_set_host(&address, cfg.wantsHost ? "127.0.0.1" : cfg.host.c_str());
        address.port = (enet_uint16)cfg.port;
        ENetPeer* peer = enet_host_connect(host, &address, proto::CH_COUNT, 0);
        Check(peer != nullptr, "enet connect failed");
        bool online = false;
        uint64_t deadline = NowMs() + 180000;
        while (NowMs() < deadline && !FileExists(coopDir + L"probe-stop")) {
            ENetEvent event{};
            if (enet_host_service(host, &event, 30) <= 0) continue;
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                proto::Hello hello;
                hello.protocol = kProtocolVersion; hello.version = COOP_VERSION_STR;
                hello.name = cfg.nickname; hello.password = cfg.password; hello.wantsHost = cfg.wantsHost;
                hello.exeSize = kSupportedExeSize;
                Writer writer(proto::C_HELLO); hello.Write(writer);
                enet_peer_send(peer, proto::CH_RELIABLE, enet_packet_create(writer.Data(), writer.Size(), ENET_PACKET_FLAG_RELIABLE));
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                Reader reader(event.packet->data, event.packet->dataLength);
                uint8_t message = reader.U8();
                if (message == proto::S_WELCOME) {
                    proto::Welcome welcome; Check(welcome.Read(reader), "bad welcome");
                    std::string data = "{\"serverName\":" + Json(welcome.serverName) + ",\"friendlyFire\":" +
                        (welcome.friendlyFire ? "true" : "false") + ",\"nickname\":" + Json(cfg.nickname) +
                        ",\"skin\":" + std::to_string(cfg.skin) + ",\"port\":" + std::to_string(cfg.port) + "}";
                    WriteFileBytes(coopDir + L"probe-result.json", data.data(), data.size());
                    cfg.WriteLauncherStatus("online"); online = true;
                } else if (message == proto::S_REJECT) {
                    reader.Str(); const std::string reason = reader.Str(); cfg.WriteLauncherStatus("failed", reason);
                    enet_packet_destroy(event.packet); enet_host_destroy(host); enet_deinitialize(); return 2;
                }
                enet_packet_destroy(event.packet);
            } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                cfg.WriteLauncherStatus("offline", "Server disconnected"); break;
            }
        }
        cfg.WriteLauncherStatus(online ? "offline" : "failed", online ? "" : "Native connection timed out");
        enet_host_destroy(host); enet_deinitialize();
        return online ? 0 : 1;
    } catch (const std::exception& error) { cfg.WriteLauncherStatus("failed", error.what()); std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
