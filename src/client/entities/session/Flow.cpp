#include "entities/session/Flow.h"

#include <windows.h>

#include <cmath>
#include <cstring>

#include "common/api/nat-traversal/NatTraversal.h"
#include "common/api/protocol/Protocol.h"
#include "common/lib/ini/Ini.h"
#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "entities/mission/Mission.h"
#include "entities/player/Players.h"
#include "entities/profile/Profile.h"
#include "entities/vehicle/Vehicles.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"

namespace coop::flow {

using namespace proto;

namespace {
Phase g_phase = Phase::Idle;
std::string g_error;
int g_hostSlot = 0;          // slot chosen for hosting (0 = current game)
uint16_t g_port = 7788;
std::string g_password, g_hostAddr;
bool g_isHosting = false;
uint64_t g_phaseTime = 0;
int g_connectAttempts = 0;

HANDLE g_serverProcess = nullptr;
HANDLE g_serverJob = nullptr;

bool g_worldUploadPending = false;  // host: a partner asked for a fresh world
bool g_keepSent = false;            // host: the stored world goes out meanwhile, a fresh one once it can be saved
bool g_prologue = false;            // host: a new game's prologue runs (it cannot be saved)
bool g_afterPrologue = false;       // host: and is over, no save of this game has gone out yet
bool g_prologueWait = false;        // partner: the host plays a new game's prologue
uint64_t g_gameStartedAt = 0, g_snapshotTriedAt = 0;
bool g_follower = false;            // partner: in a new game of its own next to the host, until its first save
bool g_progressUpload = false;      // host: the next snapshot goes to the partners in the game as well
std::vector<uint8_t> g_pendingWorld;  // partner: the host's world after its progress, loaded at a safe moment
SavedSpot g_spot;
bool g_hasSpot = false;

void SetPhase(Phase p) {
    g_phase = p;
    g_phaseTime = NowMs();
}

void Fail(const std::string& why) {
    g_error = why;
    g_hasSpot = false;  // a reload that failed: its spot is no use to the next world
    g_pendingWorld.clear();
    LOGW("Flow error: %s", why.c_str());
    ui::TheChat().AddError(why);
    Net().Disconnect();
    SetPhase(Phase::Idle);
}

bool StartServerProcess(const std::wstring& worldPath, bool newGame) {
    auto& cfg = Cfg();
    std::wstring exe = cfg.coopDir + L"coop-server.exe";
    if (!FileExists(exe)) {
        Fail("Нет файла coop\\coop-server.exe. Переустановите мод.");
        return false;
    }
    std::wstring cmd = L"\"" + exe + L"\" --port " + std::to_wstring(g_port) + L" --data \"" + cfg.coopDir +
                       Utf8ToWide(cfg.serverData) + L"\" --name \"" + Utf8ToWide(cfg.nickname) + L"\" --host \"" +
                       Utf8ToWide(cfg.nickname) + L"\"";
    if (!g_password.empty()) cmd += L" --password \"" + Utf8ToWide(g_password) + L"\"";
    if (!worldPath.empty()) cmd += L" --world \"" + worldPath + L"\"";
    if (newGame) cmd += L" --new-game";

    if (!g_serverJob) {
        g_serverJob = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_serverJob, JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        cfg.coopDir.c_str(), &si, &pi)) {
        Fail(Format("Не удалось запустить сервер (ошибка %lu).", GetLastError()));
        return false;
    }
    // the server dies together with the game
    AssignProcessToJobObject(g_serverJob, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    g_serverProcess = pi.hProcess;
    LOGI("Local server started (pid %lu)", pi.dwProcessId);
    return true;
}

void StopServerProcess() {
    if (g_serverProcess) {
        TerminateProcess(g_serverProcess, 0);
        CloseHandle(g_serverProcess);
        g_serverProcess = nullptr;
    }
}

bool ServerProcessAlive() {
    return g_serverProcess && WaitForSingleObject(g_serverProcess, 0) == WAIT_TIMEOUT;
}

void SaveConnectSettings() {
    auto& cfg = Cfg();
    Ini ini;
    ini.Load(cfg.iniPath);
    ini.Set("player", "name", cfg.nickname);
    ini.Set("connect", "mode", cfg.wantsHost ? "host" : "join");  // the menu opens on the same tab next time
    ini.Set("connect", "host", cfg.host);
    ini.SetInt("connect", "port", cfg.port);
    ini.Set("connect", "password", cfg.password);
    ini.Save(cfg.iniPath);
}

void UploadWorld(const std::vector<uint8_t>& data, const std::string& label) {
    Writer b(C_WORLD_BEGIN);
    b.U32((uint32_t)data.size());
    b.U32(Crc32(data.data(), data.size()));
    b.Str(label, 128);
    Net().Send(CH_BULK, b);
    for (size_t off = 0; off < data.size(); off += kWorldChunk) {
        size_t n = std::min(kWorldChunk, data.size() - off);
        Writer c(C_WORLD_CHUNK);
        c.U32((uint32_t)off);
        c.Bytes(data.data() + off, n);
        Net().Send(CH_BULK, c);
    }
    Writer e(C_WORLD_END);
    Net().Send(CH_BULK, e);
    LOGI("World snapshot uploaded (%u bytes)", (unsigned)data.size());
}

void OnOnline() {
    auto& net = Net();
    if (net.IsHost()) {
        if (g_hostSlot > 0 && !core::IsInGame()) {
            ui::TheChat().AddSystem(Format("Загружаем сохранение из слота %d...", g_hostSlot));
            if (!core::LoadSaveFile(core::SlotPath(g_hostSlot))) {
                Fail(Format("Не удалось загрузить сохранение из слота %d.", g_hostSlot));
                return;
            }
            SetPhase(Phase::LoadingWorld);
        } else if (g_hostSlot < 0 && !core::IsInGame()) {
            ui::TheChat().AddSystem("Сервер создан. Начинаем новую игру...");
            core::StartNewGame();
            SetPhase(Phase::LoadingWorld);
        } else {
            SetPhase(core::IsInGame() ? Phase::Playing : Phase::Idle);
            if (!core::IsInGame()) ui::TheChat().AddSystem("Сервер создан. Начните новую игру или загрузите сохранение.");
            else g_worldUploadPending = true;  // give the server a fresh world
        }
    } else {
        g_follower = g_prologueWait = false;  // a new session: the server says again whether the host is in a prologue
        ui::TheChat().AddSystem("Запрашиваем мир у хоста...");
        Writer w(C_WORLD_REQUEST);
        net.Send(CH_BULK, w);
        SetPhase(Phase::WaitingWorld);
    }
}

}  // namespace

Phase GetPhase() { return g_phase; }
const std::string& LastError() { return g_error; }

std::wstring PartnerWorldPath() { return Cfg().coopDir + L"world\\partner_world.b"; }

std::string StatusText() {
    auto& net = Net();
    switch (g_phase) {
        case Phase::Idle:
            if (net.Online()) return net.IsHost() ? "Сервер работает" : "Подключено";
            return g_error.empty() ? "Нет подключения" : g_error;
        case Phase::StartingServer:
            return "Запускаем сервер...";
        case Phase::Connecting: {
            if (g_isHosting) return "Запускаем сервер...";
            nat::Endpoint ep;
            uint8_t key = 0;
            if (nat::ParseInviteCode(g_hostAddr, ep, key)) return "Подключаемся по коду...";
            return Format("Подключаемся к %s:%u...", g_hostAddr.c_str(), g_port);
        }
        case Phase::WaitingWorld: {
            float p = net.WorldProgress();
            if (p >= 0) return Format("Получаем мир хоста: %d%%", (int)(p * 100));
            return g_prologueWait ? "Хост проходит пролог" : "Ждём мир от хоста...";
        }
        case Phase::LoadingWorld:
            return "Загружаем мир...";
        case Phase::Playing:
            return net.IsHost() ? "Вы хост" : "Вы в мире хоста";
    }
    return "";
}

std::vector<SaveSlotInfo> ListSaveSlots() {
    std::vector<SaveSlotInfo> out;
    for (int s = 1; s <= 8; ++s) {
        SaveSlotInfo info;
        info.slot = s;
        std::vector<uint8_t> data;
        std::wstring path = core::SlotPath(s);
        if (FileExists(path) && ReadFileBytes(path, data) && data.size() > 0x200 && memcmp(data.data(), "BLOCK", 5) == 0) {
            info.exists = true;
            // CSimpleVariablesSaveStructure follows the "BLOCK" tag; the save name is GXT-encoded,
            // so only show it when it is plain ASCII.
            const uint8_t* sv = data.data() + 5;
            std::string name;
            for (int i = 0; i < 100 && sv[4 + i]; ++i) name += (char)sv[4 + i];
            bool ascii = !name.empty();
            for (char c : name)
                if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) ascii = false;
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad);
            SYSTEMTIME st{};
            FILETIME local;
            FileTimeToLocalFileTime(&fad.ftLastWriteTime, &local);
            FileTimeToSystemTime(&local, &st);
            info.title = Format("Слот %d · %02d.%02d.%04d, %02d:%02d", s, st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
            if (ascii) info.title += " · " + name;
        } else {
            info.title = Format("Слот %d · пусто", s);
        }
        out.push_back(info);
    }
    return out;
}

void Host(int slot, uint16_t port, const std::string& password) {
    auto& cfg = Cfg();
    g_error.clear();
    g_isHosting = true;
    g_hostSlot = slot;
    g_port = port;
    g_password = password;
    cfg.wantsHost = true;
    cfg.port = port;
    cfg.password = password;
    SaveConnectSettings();
    std::wstring world;
    if (slot > 0) {
        world = core::SlotPath(slot);
        if (!FileExists(world)) {
            Fail(Format("Слот %d пуст.", slot));
            return;
        }
    } else if (slot == 0 && core::IsPlayerReady()) {
        std::vector<uint8_t> snap;
        if (core::SaveWorldSnapshot(snap)) world = Cfg().coopDir + L"world\\host_snapshot.b";
    }
    StopServerProcess();
    if (!StartServerProcess(world, slot < 0)) return;
    SetPhase(Phase::StartingServer);
    g_connectAttempts = 0;
}

void Join(const std::string& hostArg, uint16_t port, const std::string& password) {
    auto& cfg = Cfg();
    // "file:<path>": the invite code written by a server (dedicated servers, test setups)
    std::string host = hostArg;
    if (host.rfind("file:", 0) == 0) {
        std::vector<uint8_t> data;
        std::wstring path = Utf8ToWide(host.substr(5));
        if (path.size() < 2 || path[1] != L':') path = cfg.coopDir + path;
        if (ReadFileBytes(path, data)) host = Trim(std::string(data.begin(), data.end()));
    }
    g_error.clear();
    g_isHosting = false;
    g_hostAddr = host;
    g_port = port;
    g_password = password;
    cfg.host = hostArg;  // keep "file:" so the next start reads a fresh code
    cfg.wantsHost = false;
    cfg.port = port;
    cfg.password = password;
    SaveConnectSettings();
    if (core::IsInGame()) {
        ui::TheChat().AddSystem("Подключаемся. Вместо текущей игры загрузится мир хоста.");
    }
    Net().Connect(host, port, cfg.nickname, password, false);
    SetPhase(Phase::Connecting);
    g_connectAttempts = 1;
}

void Leave() {
    Net().Disconnect();
    StopServerProcess();
    g_isHosting = g_follower = g_prologueWait = g_progressUpload = g_hasSpot = false;
    g_pendingWorld.clear();
    SetPhase(Phase::Idle);
    ui::TheChat().AddSystem("Вы вышли из совместной игры.");
}

namespace {
void LoadHostWorld(const std::vector<uint8_t>& data);

// a reload must not cut into anything: on foot, alive, no cutscene, no mission here or at the host's
bool SafeToReload() {
    game::CPlayerPed* me = game::FindPlayerPed(0);
    return core::IsPlayerReady() && me && !me->InVehicle() && me->Health() > 0.0f && !game::Ref<uint8_t>(0xB5F851) &&
           !core::IsOnMission() && !HostMissionRecent(5000);
}

void SaveSpot() {
    game::CPlayerPed* me = game::FindPlayerPed(0);
    game::CVector p = me->GetPosition();
    g_spot = {};
    g_spot.x = p.x, g_spot.y = p.y, g_spot.z = p.z;
    g_spot.heading = me->CurrentRotation();
    g_spot.area = me->AreaCode();
    g_spot.health = me->Health();
    g_spot.armour = me->Armour();
    g_spot.activeWeapon = me->ActiveWeapon().type;
    for (int i = 0; i < 13; ++i) g_spot.weapons[i] = me->Weapons()[i].type, g_spot.ammo[i] = me->Weapons()[i].ammoTotal;
    // the car comes back only if it is free, whole and this game's: a wreck would burn out again next to the partner,
    // and a car someone sits in, or the other player's, would stand twice
    game::CVehicle* car = Players::Instance().LastCar();
    NetVehicle* nv = car ? Vehicles::Instance().Find(car) : nullptr;
    bool empty = car && !car->Driver() && car->NumPassengers() == 0;
    if (empty && car->Status() != 5 && car->Health() >= 250.0f && !(nv && Vehicles::Instance().IsMirror(*nv)) &&
        (car->GetPosition() - p).Length() < 60.0f) {
        game::CVector c = car->GetPosition();
        game::CMatrix* m = car->Matrix();
        g_spot.carModel = car->ModelIndex();
        g_spot.carX = c.x, g_spot.carY = c.y, g_spot.carZ = c.z;
        g_spot.carHeading = m ? std::atan2(-m->forward.x, m->forward.y) : 0.0f;
        g_spot.carHealth = car->Health();
        memcpy(g_spot.carColors, car->Colors(), 4);
    }
    g_hasSpot = true;
}
}  // namespace

void Update() {
    auto& net = Net();
    // report our status to the server (used for world hand-over and the player list)
    static int lastStatus = -1;
    int status = core::IsPlayerReady() ? STATUS_INGAME : (game::GameState() == game::GAME_STATE_IDLE ? STATUS_LOADING : STATUS_MENU);
    if (net.Online() && status != lastStatus) {
        Writer w(C_STATUS);
        w.U8((uint8_t)status);
        net.Send(CH_RELIABLE, w);
        lastStatus = status;
    }
    if (!net.Online()) lastStatus = -1;
    static bool wasOnline = false;
    bool online = net.Online();
    if (online && !wasOnline) OnOnline();
    if (!online && wasOnline && g_phase != Phase::Idle) {
        SetPhase(Phase::Idle);
        g_error = net.FailReason();
    }
    wasOnline = online;

    switch (g_phase) {
        case Phase::StartingServer:
            if (!ServerProcessAlive()) {
                Fail("Сервер не запустился. Возможно, порт уже занят. Подробности в файле coop\\server-data\\server.log.");
                break;
            }
            if (NowMs() - g_phaseTime > 400) {
                net.Connect("127.0.0.1", g_port, Cfg().nickname, g_password, true);
                SetPhase(Phase::Connecting);
                g_connectAttempts = 1;
            }
            break;
        case Phase::Connecting:
            if (net.GetState() == Session::State::Failed) {
                if (g_isHosting && g_connectAttempts < 5 && ServerProcessAlive()) {
                    ++g_connectAttempts;
                    net.Connect("127.0.0.1", g_port, Cfg().nickname, g_password, true);
                } else {
                    Fail(net.FailReason());
                }
            }
            break;
        case Phase::WaitingWorld:
            if (NowMs() - g_phaseTime > 120000 && net.WorldProgress() < 0 && !g_prologueWait) Fail("Хост так и не прислал мир.");
            break;
        case Phase::LoadingWorld:
            break;
        case Phase::Playing:
            if (!g_pendingWorld.empty() && !net.IsHost() && SafeToReload()) {
                std::vector<uint8_t> data = std::move(g_pendingWorld);
                g_pendingWorld.clear();
                SaveSpot();
                SaveProfileNow();  // money, skills and look as they are now go on top of the new world
                ui::TheChat().AddSystem("Хост продвинулся по сюжету. Загружаем его мир, вы останетесь на своём месте...");
                LoadHostWorld(data);
            }
            break;
        default:
            break;
    }

    // host: deliver a fresh snapshot of the world when someone needs it. A new game's prologue is a mission and cannot
    // be saved: the host plays it alone, partners wait. Once it is over (CJ gets on the bike, still unsaveable) they
    // join in a new game of their own, and the first save goes to them.
    if (online && net.IsHost() && core::IsPlayerReady()) {
        bool prologue = core::InPrologue();
        if (prologue != g_prologue) {
            g_prologue = prologue;
            Writer w(prologue ? C_WORLD_NEWGAME : C_WORLD_FOLLOW);  // NEWGAME: the server's world is an older game's
            net.Send(CH_BULK, w);
            if (!prologue) g_worldUploadPending = g_afterPrologue = true;
            LOGI(prologue ? "New game: its prologue runs, partners wait for its end" : "The prologue is over: partners join");
        }
        // a new game launches its prologue in the first frames: no snapshot before it does
        if (g_worldUploadPending && !prologue && NowMs() - g_gameStartedAt > 5000 && NowMs() - g_snapshotTriedAt > 1000) {
            g_snapshotTriedAt = NowMs();
            std::vector<uint8_t> snap;
            if (core::SaveWorldSnapshot(snap)) {
                UploadWorld(snap, g_progressUpload ? "progress" : "host snapshot");  // the server passes "progress" on
                g_progressUpload = false;
                g_worldUploadPending = g_keepSent = g_afterPrologue = false;
            } else if (g_afterPrologue && !g_keepSent) {  // the prologue's bike: who asks now joins in a new game
                Writer w(C_WORLD_FOLLOW);
                net.Send(CH_BULK, w);
                g_keepSent = true;
            } else if (!g_keepSent && (core::IsOnMission() || game::FindPlayerPed(0)->InVehicle())) {
                // no snapshot during a mission or in a vehicle: the stored world now, a fresh one once possible
                Writer w(C_WORLD_KEEP);
                net.Send(CH_BULK, w);
                g_keepSent = true;
            }
        }
    }
}

void RequestFreshWorld() {
    g_worldUploadPending = true;
    g_keepSent = false;  // a new request gets its own answer
}

void OnHostInPrologue() {
    if (Net().IsHost() || g_prologueWait) return;
    g_prologueWait = true;
    g_pendingWorld.clear();  // the old game's progress is no world to load any more
    g_hasSpot = false;
    g_phaseTime = NowMs();
    ui::TheChat().AddSystem("Хост проходит пролог. Вы появитесь рядом с ним, когда CJ сядет на велосипед.");
}

void OnNewGameFromHost() {
    if (Net().IsHost()) return;
    ui::TheChat().AddSystem("Хост прошёл пролог. Загружаемся, вы появитесь рядом с ним...");
    g_follower = true;
    g_prologueWait = false;
    core::StartNewGame();
    SetPhase(Phase::LoadingWorld);
}

bool IsFollower() { return g_follower && Net().Online() && !Net().IsHost(); }

bool InHostWorld() { return Net().Online() && g_phase == Phase::Playing && !g_prologueWait; }

// The story moved on in the host's game (a mission passed, the call that unlocks something after it): a new
// snapshot goes to the partners already playing, who load it, staying where they are.
void WorldProgressed() {
    if (!Net().Online() || !Net().IsHost()) return;
    g_worldUploadPending = g_progressUpload = true;
    LOGI("Story progress: the partners get the world again");
}

bool TakeSavedSpot(SavedSpot& out) {
    if (!g_hasSpot) return false;
    out = g_spot;
    g_hasSpot = false;
    return true;
}


namespace {
void LoadHostWorld(const std::vector<uint8_t>& data) {
    EnsureDirectory(Cfg().coopDir + L"world");
    std::wstring path = PartnerWorldPath();
    if (!WriteFileBytes(path, data.data(), data.size())) {
        Fail("Не удалось записать сохранение мира на диск.");
        return;
    }
    ui::TheChat().AddSystem(g_follower ? "Пролог позади. Загружаем мир хоста..." : "Мир хоста получен. Загружаем...");
    g_follower = g_prologueWait = false;
    if (!core::LoadSaveFile(path)) {
        Fail("Мир хоста не загрузился. Скорее всего, у вас разные версии игры или модов.");
        return;
    }
    SetPhase(Phase::LoadingWorld);
}
}  // namespace

void OnWorldReceived(const std::vector<uint8_t>& data) {
    if (Net().IsHost()) return;
    if (g_phase == Phase::Playing && core::IsPlayerReady() && !g_follower) {  // the host's progress: at a safe moment
        g_pendingWorld = data;
        LOGI("The host's world after its progress came: loaded at a safe moment");
        return;
    }
    LoadHostWorld(data);
}

void OnGameStarted() {
    g_gameStartedAt = NowMs();
    g_prologue = g_afterPrologue = false;  // another world: its own story
    if (g_phase == Phase::LoadingWorld || (Net().Online() && g_phase != Phase::Playing)) SetPhase(Phase::Playing);
    // the host's save carries the host's wanted level: the partner starts without it
    if (Net().Online() && !Net().IsHost()) game::script::Cmd(0x0110).I(0).Run();  // CLEAR_WANTED_LEVEL
    if (Net().Online() && Net().IsHost()) g_worldUploadPending = true;
}

void OnGameShutdown() {
    if (g_phase == Phase::Playing) SetPhase(Phase::LoadingWorld);
}

}  // namespace coop::flow
