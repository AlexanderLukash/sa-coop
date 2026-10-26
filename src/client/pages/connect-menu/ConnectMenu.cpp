#include "pages/connect-menu/ConnectMenu.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <map>

#include "common/api/nat-traversal/NatTraversal.h"
#include "common/config/version/Version.h"
#include "common/lib/util/Util.h"
#include "entities/session/Flow.h"
#include "features/balance/Balance.h"
#include "features/street-racing/StreetRacing.h"
#include "entities/economy/Economy.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/game/SkinCatalog.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"
#include "shared/ui/input/Input.h"
#include "shared/ui/renderer/Renderer.h"
#include "shared/ui/widgets/Widgets.h"

namespace coop::ui {

namespace {
using w::kSaBlue, w::kSaSlate, w::kSaMuted, w::kSaRed, w::kSaLine, w::kSaGreen;
constexpr uint32_t kText = 0xFFE6E9EF;
constexpr uint32_t kGold = 0xFFC1A1FF;  // the game's mission-title cream

bool g_visible = false;
bool g_autoShown = false;
int g_tab = 0;  // 0 "connect", 1 "create"
std::wstring g_nick, g_addr, g_port, g_password;
std::string g_formError;
int g_slotIndex = 0;  // index into the slot list
std::vector<flow::SaveSlotInfo> g_slots;
uint64_t g_slotsTime = 0;
uint64_t g_copiedMs = 0;
bool g_skinBrowser = false;
bool g_balanceEditor = false;
bool g_streetBrowser=false,g_privateEditor=false;
int g_eventIndex=0,g_routeIndex=0,g_creditIndex=0,g_raceCapacity=2;
std::wstring g_raceStake=L"100";
std::map<uint8_t,std::string> g_raceInvites;
uint8_t g_balanceTarget = 255;
std::string g_balanceIdentity;
std::wstring g_balanceAmount;
std::string g_balanceStatus;
std::wstring g_skinSearch;
int g_skinPage = 0;

void CopyToClipboard(const std::string& utf8) {
    std::wstring text = Utf8ToWide(utf8);
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t))) {
        memcpy(GlobalLock(mem), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        else g_copiedMs = NowMs();
    }
    CloseClipboard();
}

// the save slots and, after them, a new game (the host plays its prologue alone, partners join at the bike)
void RefreshSlots() {
    g_slots = flow::ListSaveSlots();
    flow::SaveSlotInfo fresh;
    fresh.slot = -1;
    fresh.exists = true;
    fresh.title = TR("Новая игра", "New game");
    g_slots.push_back(fresh);
    g_slotsTime = NowMs();
}

void LoadFields() {
    auto& cfg = Cfg();
    g_nick = Widen(cfg.nickname);
    g_addr = Widen(cfg.host);
    g_port = std::to_wstring(cfg.port);
    g_password = Widen(cfg.password);
    g_tab = cfg.wantsHost ? 1 : 0;
    g_formError.clear();
    RefreshSlots();
    // preselect the first save; without any, the new game
    g_slotIndex = 0;
    for (size_t i = 0; i < g_slots.size(); ++i)
        if (g_slots[i].exists) {
            g_slotIndex = (int)i;
            break;
        }
}

bool ApplyNick() {
    std::string nick = SanitizeNickname(Narrow(g_nick));
    if (nick.size() < 2) {
        g_formError = TR("В нике должно быть от 2 до 20 символов (буквы, цифры, знаки _-.[]).",
                         "A nickname needs 2 to 20 characters (letters, digits, _-.[]).");
        w::SetFocus("nick");
        return false;
    }
    g_formError.clear();
    Cfg().nickname = nick;
    g_nick = Widen(nick);
    return true;
}

uint16_t PortValue() {
    int p = _wtoi(g_port.c_str());
    if (p <= 0 || p > 65535) p = 7788;
    return (uint16_t)p;
}
const char* StreetPhase(street::Phase phase){
    switch(phase){
    case street::Phase::Scheduled:return TR("Событие готовится","Scheduled");
    case street::Phase::Registration:return TR("Регистрация","Registration");
    case street::Phase::Countdown:return TR("Обратный отсчёт","Countdown");
    case street::Phase::Racing:return TR("Гонка идёт","Racing");
    case street::Phase::Finished:return TR("Определяем результаты","Finalizing results");
    case street::Phase::Settled:return TR("Расчёт завершён","Settled");
    default:return TR("Событие закрыто","Closed");
    }
}
}  // namespace

void SetConnectMenuVisible(bool visible) {
    if (visible == g_visible) return;
    g_visible = visible;
    if (visible) {
        g_balanceEditor = false;
        g_streetBrowser=g_privateEditor=false;
        LoadFields();
        if (TheChat().IsOpen()) TheChat().Close();
        In().SetKeyboardCapture(true);
        In().SetMouseCapture(true);
    } else {
        In().SetKeyboardCapture(false);
        In().SetMouseCapture(false);
    }
}

bool IsConnectMenuVisible() { return g_visible; }

void ProcessConnectMenu() {
    // Show the window once on the first main-menu frame so the player sees how to start co-op.
    if (!g_autoShown && game::GameState() == game::GAME_STATE_FRONTEND_IDLE && !Cfg().autoConnect) {
        g_autoShown = true;
        SetConnectMenuVisible(true);
    }
    if (g_visible && NowMs() - g_slotsTime > 3000) RefreshSlots();
}

void RenderConnectMenu() {
    if (!g_visible) return;
    auto& g = Gfx();
    float s = g.Scale();
    auto& net = Net();
    bool online = net.Online();
    auto phase = flow::GetPhase();
    bool busy = phase == flow::Phase::StartingServer || phase == flow::Phase::Connecting ||
                phase == flow::Phase::WaitingWorld || phase == flow::Phase::LoadingWorld;
    bool inGame = core::IsPlayerReady();

    float lh = g.LineHeight(FONT_NORMAL), sh = g.LineHeight(FONT_SMALL), mh = g.LineHeight(FONT_MENU),
          ch = g.LineHeight(FONT_MENU_SMALL), bh = g.LineHeight(FONT_BIG);
    float rowH = lh + 14 * s;
    float W = 940 * s, H = (online || busy || g_skinBrowser ? 950 : 820) * s;
    float x = (g.Width() - W) * 0.5f, y = (g.Height() - H) * 0.5f;
    float cx = x + 52 * s, cw = W - 104 * s;
    float room = 12 * s;

    float fy = y + H - 20 * s - sh;       // key legend
    float by = fy - 14 * s - rowH;        // footer items
    float bodyBottom = by - 12 * s;

    g.Rect(0, 0, g.Width(), g.Height(), 0xB00E0917);
    w::BeginFrame();
    float cy = w::Panel(x, y, W, H, "SA:COOP") + 8 * s;
    float vx = x + W - 52 * s;
    g.Text(FONT_MENU_SMALL, vx, y + 36 * s, "v" COOP_VERSION_STR, kSaSlate, TEXT_RIGHT);
    // the language switch left of the version, the pointer at the active one; the choice is saved at once
    float lx = vx - g.TextWidth(FONT_MENU_SMALL, "v" COOP_VERSION_STR) - 34 * s;
    for (int en = 1; en >= 0; --en) {
        const char* label = en ? "EN" : "RU";
        float tw = g.TextWidth(FONT_MENU_SMALL, label);
        lx -= tw;
        if (w::Tab(en ? "lang1" : "lang0", lx - 6 * s, y + 32 * s, tw + 12 * s, ch + 8 * s, label, Cfg().english == (en != 0), true)) {
            Cfg().english = en != 0;
            flow::SaveConnectSettings();
            RefreshSlots();
        }
        lx -= 52 * s;  // room for the pointer of EN
    }

    auto caption = [&](float px, const std::string& text) { g.Text(FONT_MENU_SMALL, px, cy, text, kSaMuted); };
    // wrapped paragraph, advances cy
    auto para = [&](const std::string& text, uint32_t color, FontId f = FONT_SMALL, bool centre = false) {
        for (const std::wstring& line : g.Wrap(f, Widen(text), cw)) {
            g.TextW(f, centre ? cx + cw * 0.5f : cx, cy, line, color, TEXT_SHADOW | (centre ? TEXT_CENTER : 0));
            cy += g.LineHeight(f);
        }
    };
    // a menu item whose text starts at px (the pointer hangs to the left of it)
    auto item = [&](const char* id, float px, float py, const std::string& label, bool enabled = true, bool lit = false) {
        return w::Button(id, px - room, py, g.TextWidth(FONT_MENU, label) + 2 * room, rowH, label, enabled, lit);
    };

    if (g_balanceEditor && online && net.IsHost()) {
        g.Text(FONT_BIG, cx, cy, TR("Баланс игроков", "Player balances"), kText, 0);
        cy += bh + 18 * s;
        std::vector<uint8_t> targets{net.MyId()};
        for (const auto& [id, peer] : net.Peers()) targets.push_back(id);
        auto selected = std::find(targets.begin(), targets.end(), g_balanceTarget);
        if (selected == targets.end()) {
            g_balanceTarget = net.MyId(); selected = targets.begin();
            g_balanceAmount.clear(); g_balanceStatus.clear();
        }
        if(g_balanceIdentity!=net.PeerName(g_balanceTarget)){
            g_balanceIdentity=net.PeerName(g_balanceTarget);g_balanceAmount.clear();g_balanceStatus.clear();
        }
        int index = (int)(selected - targets.begin());
        int step = w::Selector("balance-player", cx, cy, cw, rowH, net.PeerName(g_balanceTarget));
        if (step) {
            index = (index + step + (int)targets.size()) % (int)targets.size();
            g_balanceTarget = targets[index];
            g_balanceIdentity=net.PeerName(g_balanceTarget);
            g_balanceAmount.clear();
            g_balanceStatus.clear();
        }
        cy += rowH + 18 * s;
        caption(cx, TR("Новый баланс (0–999999999)", "New balance (0–999999999)"));
        cy += ch + 6 * s;
        bool enter = w::TextBox("balance-amount", cx, cy, cw, rowH, g_balanceAmount, 9, false, true);
        cy += rowH + 18 * s;
        bool valid = !g_balanceAmount.empty() && g_balanceAmount.size() <= 9 &&
            std::all_of(g_balanceAmount.begin(), g_balanceAmount.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
        if ((w::Button("balance-apply", cx, cy, cw, rowH, TR("Установить баланс", "Set balance"), valid) || (enter && valid))) {
            if (SetPlayerBalance(g_balanceTarget, _wtoi(g_balanceAmount.c_str())))
                g_balanceStatus = TR("Запрос принят. Результат появится в чате.", "Request accepted. Check the chat for the result.");
            else g_balanceStatus = TR("Игрок недоступен.", "Player unavailable.");
        }
        cy += rowH + 18 * s;
        para(g_balanceStatus, kSaMuted);
        if(const auto* state=EconomySnapshot();state&&!state->credits.empty()){
            cy+=18*s;caption(cx,TR("Подтверждение дохода", "Income approvals"));cy+=ch+6*s;
            g_creditIndex=std::clamp(g_creditIndex,0,(int)state->credits.size()-1);
            const auto& credit=state->credits[g_creditIndex];
            int step=w::Selector("credit",cx,cy,cw,rowH,Format("%s · $%d",credit.account.c_str(),credit.amount));
            if(step)g_creditIndex=(g_creditIndex+step+(int)state->credits.size())%(int)state->credits.size();
            cy+=rowH+10*s;
            if(w::Button("credit-approve",cx,cy,cw*0.48f,rowH,TR("Подтвердить", "Approve")))DecideCashIncome(credit.id,true);
            if(w::Button("credit-reject",cx+cw*0.52f,cy,cw*0.48f,rowH,TR("Отклонить", "Reject")))DecideCashIncome(credit.id,false);
        }
    } else if(g_streetBrowser&&online){
        using namespace street;
        const auto* state=EconomySnapshot();
        g.Text(FONT_BIG,cx,cy,TR("Уличные гонки","Street racing"),kText,0);cy+=bh+16*s;
        if(!state)para(TR("Получаем состояние сервера…","Waiting for the server…"),kSaMuted);
        else {
            para(Format(TR("Баланс: $%d · победы %u/%u · репутация %u","Cash: $%d · wins %u/%u · reputation %u"),
                state->cash,state->statistics.wins,state->statistics.races,state->statistics.reputation),kSaBlue);
            cy+=12*s;
            if(!state->unlocked)para(TR("Выиграйте первую гонку Сезара — High Stakes, Low-Rider.","Win Cesar's first race: High Stakes, Low-Rider."),kSaMuted);
            else if(!state->available)para(TR("Гонки временно недоступны во время сюжетной миссии.","Races are temporarily unavailable during a story mission."),kSaMuted);
            else if(g_privateEditor){
                std::vector<const Route*> routes;
                for(const auto& route:Routes())if(Available(route,state->unlocked,state->city))routes.push_back(&route);
                if(!routes.empty()){
                    g_routeIndex=std::clamp(g_routeIndex,0,(int)routes.size()-1);const Route& route=*routes[g_routeIndex];
                    int step=w::Selector("private-route",cx,cy,cw,rowH,route.name);
                    if(step)g_routeIndex=(g_routeIndex+step+(int)routes.size())%(int)routes.size();cy+=rowH+12*s;
                    para(Format(TR("%.1f км · сложность %u/3 · ставка до $%u","%.1f km · difficulty %u/3 · stake up to $%u"),route.Length()/1000,route.difficulty,route.maximumStake),kSaMuted);
                    para(Format(TR("Классы: %s","Classes: %s"),AllowedClasses(route).c_str()),kSaMuted);
                    cy+=10*s;caption(cx,TR("Ставка каждого участника", "Stake per participant"));cy+=ch+6*s;
                    w::TextBox("race-stake",cx,cy,cw,rowH,g_raceStake,7,false,true);cy+=rowH+10*s;
                    int capacity=w::Selector("race-capacity",cx,cy,cw,rowH,Format(TR("Мест: %d","Places: %d"),g_raceCapacity));
                    g_raceCapacity=std::clamp(g_raceCapacity+capacity,2,8);cy+=rowH+14*s;
                    caption(cx,TR("Пригласить игроков", "Invite players"));cy+=ch+8*s;
                    for(const auto& [id,peer]:net.Peers()){
                        if(g_raceInvites.count(id)&&g_raceInvites.at(id)!=peer.name)g_raceInvites.erase(id);
                        bool invited=g_raceInvites.count(id)!=0;std::string control="race-invite-"+std::to_string(id);
                        if(w::Checkbox(control.c_str(),cx,cy,peer.name,invited)){if(invited)g_raceInvites[id]=peer.name;else g_raceInvites.erase(id);}cy+=lh+12*s;
                    }
                    bool valid=!g_raceStake.empty()&&g_raceStake.size()<=7&&std::all_of(g_raceStake.begin(),g_raceStake.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';})&&
                        (uint32_t)_wtoi(g_raceStake.c_str())<=route.maximumStake&&!g_raceInvites.empty();
                    if(w::Button("race-create",cx,cy,cw,rowH,TR("Создать частную гонку","Create private race"),valid)){
                        std::vector<uint8_t> invites;for(const auto& [id,name]:g_raceInvites)if(net.Peer(id)&&net.Peer(id)->name==name)invites.push_back(id);
                        CreateStreetRace(route.id,(uint32_t)_wtoi(g_raceStake.c_str()),(uint8_t)g_raceCapacity,invites);g_privateEditor=false;g_eventIndex=31;
                    }
                }
            }else{
                if(state->events.empty())para(TR("Организаторы ищут место. Новая гонка появится неожиданно.","Organizers are finding a location. A new race will appear without a fixed schedule."),kSaMuted);
                else {
                    g_eventIndex=std::clamp(g_eventIndex,0,(int)state->events.size()-1);const auto& event=state->events[g_eventIndex];const Route& route=*FindRoute(event.route);
                    int step=w::Selector("race-event",cx,cy,cw,rowH,Format("%s #%llu",route.name,(unsigned long long)event.id));
                    if(step)g_eventIndex=(g_eventIndex+step+(int)state->events.size())%(int)state->events.size();cy+=rowH+12*s;
                    para(StreetPhase(event.phase),kSaGreen);
                    uint64_t age=NowMs()-EconomyReceivedMs();unsigned seconds=(unsigned)((event.remaining>age?event.remaining-age:0)+999)/1000;
                    para(Format(TR("Взнос $%u · банк $%u · %u/%u игроков · %u с","Entry $%u · pool $%u · %u/%u players · %u s"),
                        event.fee,event.fee*(unsigned)event.participants.size(),(unsigned)event.participants.size(),event.capacity,seconds),kSaBlue);
                    para(Format(TR("%.1f км · сложность %u/3 · разрешены классы маршрута","%.1f km · difficulty %u/3 · route vehicle classes apply"),route.Length()/1000,route.difficulty),kSaMuted);
                    para(Format(TR("Классы: %s","Classes: %s"),AllowedClasses(route).c_str()),kSaMuted);
                    bool joined=std::any_of(event.participants.begin(),event.participants.end(),[&](const auto& p){return p.account==state->account;});
                    bool registration=event.phase==Phase::Registration&&seconds>0;
                    cy+=12*s;
                    if(w::Button("race-preview",cx,cy,cw*0.48f,rowH,TR("Маршрут на карте","Show route on map")))PreviewStreetRoute(event.id);
                    if(w::Button("race-join",cx+cw*0.52f,cy,cw*0.48f,rowH,joined?TR("Покинуть гонку","Leave race"):TR("Участвовать","Register"),
                        joined?event.phase<=Phase::Racing:registration)){
                        if(joined)LeaveStreetRace(event.id);else JoinStreetRace(event.id);
                    }
                    cy+=rowH+14*s;
                    for(const auto& p:event.participants){
                        const char* status=p.outcome==Outcome::Finished?TR("финиш","finished"):p.outcome==Outcome::Forfeit?TR("сход","DNF"):
                            p.outcome==Outcome::Refunded?TR("возврат","refunded"):event.phase==Phase::Racing?TR("в гонке","racing"):TR("зарегистрирован","registered");
                        para(Format("%s · %s · #%u · %.1f s",p.account.c_str(),status,p.place,p.finish/1000.0),kText);
                    }
                    if(event.privateRace&&event.organizer==state->account&&event.phase<=Phase::Registration){
                        cy+=10*s;if(w::Button("race-cancel",cx,cy,cw,rowH,TR("Отменить гонку и вернуть ставки","Cancel race and refund stakes")))CancelStreetRace(event.id);cy+=rowH;
                    }
                }
                cy+=18*s;if(w::Button("private-editor",cx,cy,cw,rowH,TR("Организовать частную гонку","Organize a private race")))g_privateEditor=true;
                cy+=rowH+8*s;
                if(net.IsHost()&&w::Button("race-test",cx,cy,cw,rowH,TR("Тестовая гонка без ставок","Test race, no bets"),!net.Peers().empty())){
                    if(!CreateStreetTestRace())ui::TheChat().AddSystem(TR("Не удалось создать тестовую гонку.","Could not create the test race."));
                }
            }
        }
    } else if (g_skinBrowser) {
        g.Text(FONT_BIG, cx, cy, TR("Выбор персонажа", "Your character"), kText, 0);
        cy += bh + 18 * s;
        w::TextBox("skin-search", cx, cy, cw, rowH, g_skinSearch, 40, false, false,
                   TR("Поиск по имени или ID", "Search by name or ID"));
        cy += rowH + 16 * s;
        std::string query = Narrow(g_skinSearch);
        auto lower = [](std::string value) { std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); }); return value; };
        query = lower(query);
        std::vector<const skins::Skin*> matches;
        for (const auto& skin : skins::kCatalog)
            if (query.empty() || lower(Format("%d %s %s", skin.id, skin.name, skin.model)).find(query) != std::string::npos) matches.push_back(&skin);
        constexpr int pageSize = 18;
        int pages = std::max(1, ((int)matches.size() + pageSize - 1) / pageSize);
        g_skinPage = std::clamp(g_skinPage, 0, pages - 1);
        float cellW = (cw - 20 * s) / 3, cellH = 60 * s;
        for (int i = 0; i < pageSize && g_skinPage * pageSize + i < (int)matches.size(); ++i) {
            const auto& skin = *matches[g_skinPage * pageSize + i];
            const std::string id = "skin-" + std::to_string(skin.id);
            if (w::Button(id.c_str(), cx + (i % 3) * (cellW + 10 * s), cy + (i / 3) * (cellH + 9 * s), cellW, cellH,
                          Format("#%d  %s", skin.id, skin.name), true, Cfg().skin == skin.id)) {
                Cfg().skin = skin.id;
                flow::SaveConnectSettings();
            }
        }
        cy += 6 * (cellH + 9 * s) + 16 * s;
        int step = w::Selector("skin-page", cx, cy, cw, rowH,
                               Format(TR("Страница %d / %d · %d скинов", "Page %d / %d · %d skins"), g_skinPage + 1, pages, (int)matches.size()));
        if (step) g_skinPage = (g_skinPage + step + pages) % pages;
        cy += rowH + 18 * s;
        const auto* selected = skins::Find(Cfg().skin);
        para(selected ? Format(TR("Выбрано: %s (#%d)", "Selected: %s (#%d)"), selected->name, selected->id) : "CJ", kSaBlue);
        para(TR("Скин применится вне транспорта и катсцен.", "Your skin changes when outside vehicles and cutscenes."), kSaMuted);
    } else if (online) {
        // ---- in a session: who we are, the code for the friend, the players
        bool host = net.IsHost();
        g.Text(FONT_BIG, cx, cy, host ? TR("Сервер запущен", "Server running") : TR("Вы в игре", "Connected"), kSaBlue,
               TEXT_SHADOW);
        cy += bh + 2 * s;
        if (!host) para(Format(TR("Сервер «%s»", "Server \"%s\""), net.ServerName().c_str()), kSaMuted, FONT_NORMAL);
        cy += 22 * s;
        if (host) {
            caption(cx, TR("Код для друга", "Code for your friend"));
            cy += ch + 6 * s;
            // our wide capitals, not the game's pricedown: translations swap letters in that font
            // (a Russian build drew 2QMY as "2qmy")
            const std::string& code = net.InviteCode();
            if (code.empty()) {
                g.Text(FONT_NORMAL, cx, cy, TR("Узнаём внешний адрес...", "Finding out the external address..."), kSaMuted, TEXT_SHADOW);
                cy += bh;
            } else {
                g.Text(FONT_BIG, cx, cy, code, kGold, TEXT_OUTLINE);
                cy += bh + 2 * s;
                bool copied = NowMs() - g_copiedMs < 1500;
                if (item("copy", cx, cy, copied ? TR("Скопировано", "Copied") : TR("Скопировать", "Copy"), true, copied))
                    CopyToClipboard(code);
                cy += rowH;
            }
            cy += 20 * s;
        }
        caption(cx, Format(TR("Игроки: %d", "Players: %d"), 1 + (int)net.Peers().size()));
        cy += ch + 8 * s;
        auto row = [&](const std::string& name, bool isHost, uint32_t ping, bool me) {
            float rh = lh + 14 * s;
            g.Rect(cx, cy, cw, rh, 0xFF251D33);
            g.Text(FONT_NORMAL, cx + 14 * s, cy + 7 * s, me ? name + TR(" (вы)", " (you)") : name, me ? 0xFFFFFFFF : kText,
                   TEXT_SHADOW);
            g.Text(FONT_MENU_SMALL, cx + cw * 0.55f, cy + (rh - ch) * 0.5f, isHost ? TR("хост", "host") : TR("напарник", "partner"),
                   isHost ? kSaBlue : kSaGreen);
            g.Text(FONT_SMALL, cx + cw - 14 * s, cy + (rh - sh) * 0.5f, Format(TR("%u мс", "%u ms"), ping), kSaMuted, TEXT_RIGHT);
            cy += rh + 4 * s;
        };
        row(Cfg().nickname, host, net.Rtt(), true);
        for (auto& [id, p] : net.Peers()) row(p.name, p.isHost, p.ping, false);
        if (host && net.Peers().empty()) para(TR("Ждём друга...", "Waiting for your friend..."), kSaMuted);
        cy += 12 * s;
        if (w::Button("skins-online", cx, cy, cw*0.48f, rowH, TR("Изменить персонажа", "Change character"))) g_skinBrowser = true;
        if(w::Button("street-races",cx+cw*0.52f,cy,cw*0.48f,rowH,TR("Уличные гонки","Street racing")))g_streetBrowser=true;
        cy += rowH + 8 * s;
        if (host && w::Button("balances", cx, cy, cw, rowH, TR("Баланс игроков", "Player balances"))) {
            g_balanceEditor = true;
            g_balanceTarget = net.MyId();
            g_balanceIdentity=net.PeerName(g_balanceTarget);
            g_balanceAmount.clear();
            g_balanceStatus.clear();
        }
    } else if (busy) {
        // ---- connecting / starting / loading, in the middle of the body
        float barH = 6 * s;
        cy += std::max(0.0f, (bodyBottom - cy - (mh * 2 + 18 * s + barH + 16 * s + sh)) * 0.5f);
        para(flow::StatusText(), kSaBlue, FONT_MENU, true);
        cy += 18 * s;
        g.Rect(cx, cy, cw, barH, 0xFF30253F);
        float p = phase == flow::Phase::WaitingWorld ? net.WorldProgress() : -1.0f;
        if (p >= 0) {
            g.Rect(cx, cy, cw * std::clamp(p, 0.0f, 1.0f), barH, kSaBlue);
        } else {  // nothing to measure: a block sliding back and forth
            float t = (float)(NowMs() % 1600) / 800.0f;
            float bw = cw * 0.25f;
            g.Rect(cx + (cw - bw) * (t < 1 ? t : 2 - t), cy, bw, barH, kSaBlue);
        }
        cy += barH + 16 * s;
        nat::Endpoint ep;
        uint8_t key = 0;
        if (phase == flow::Phase::Connecting && nat::ParseInviteCode(Trim(Narrow(g_addr)), ep, key))
            para(TR("Подключение по коду может занять до 20 секунд.", "Connecting by code can take up to 20 seconds."), kSaMuted,
                 FONT_SMALL, true);
        if (phase == flow::Phase::WaitingWorld && p < 0)
            para(TR("Хост сохраняет мир. Скоро он загрузится у вас.", "The host is saving the world. It will load for you soon."),
                 kSaMuted, FONT_SMALL, true);
    } else {
        // ---- the form: the tabs are menu items, the active one carries the pointer
        float tx = cx;
        for (int t = 0; t < 2; ++t) {
            const char* label = t ? TR("Создать игру", "Host a game") : TR("Подключиться", "Join a game");
            float tw = g.TextWidth(FONT_MENU, label);
            if (w::Tab(t ? "tab1" : "tab0", tx - room, cy, tw + 2 * room, rowH, label, g_tab == t)) {
                g_tab = t;
                g_formError.clear();
            }
            tx += tw + room * 2.2f;
        }
        cy += rowH + 4 * s;
        g.Rect(cx, cy, cw, std::max(1.0f, s), kSaLine);
        cy += 16 * s;

        caption(cx, TR("Ник", "Nickname"));
        cy += ch + 6 * s;
        bool enter = w::TextBox("nick", cx, cy, 320 * s, rowH, g_nick, 20);
        const auto* selected = skins::Find(Cfg().skin);
        if (w::Button("skins-offline", cx + 340 * s, cy, cw - 340 * s, rowH,
                      selected ? Format("%s · #%d", selected->name, selected->id) : TR("Выбрать скин", "Choose skin"))) g_skinBrowser = true;
        cy += rowH + 14 * s;

        int hostSlot = 0;
        if (g_tab == 0) {
            caption(cx, TR("Код приглашения или IP-адрес", "Invite code or IP address"));
            cy += ch + 6 * s;
            const char* paste = TR("Вставить", "Paste");
            float pw = g.TextWidth(FONT_MENU, paste) + room * 1.9f;
            enter |= w::TextBox("addr", cx, cy, cw - pw, rowH, g_addr, 64, false, false,
                                TR("например, 2QMY-7KAP-JR8Y", "for example, 2QMY-7KAP-JR8Y"));
            if (item("paste", cx + cw - g.TextWidth(FONT_MENU, paste), cy, paste)) {
                std::wstring clip = Widen(Trim(Narrow(w::ClipboardText())));
                if (!clip.empty()) g_addr = clip.substr(0, 64);
            }
        } else {
            caption(cx, TR("Сохранение", "Save"));
            cy += ch + 6 * s;
            if (inGame) {
                g.Rect(cx, cy, cw, rowH, 0xFF251D33);
                g.Text(FONT_NORMAL, cx + 12 * s, cy + (rowH - lh) * 0.5f, TR("Текущая игра", "Current game"), kText, TEXT_SHADOW);
            } else {
                int d = w::Selector("slot", cx, cy, cw, rowH, g_slots[g_slotIndex].title);
                if (d) g_slotIndex = (g_slotIndex + d + (int)g_slots.size()) % (int)g_slots.size();
                if (g_slots[g_slotIndex].exists) hostSlot = g_slots[g_slotIndex].slot;
            }
        }
        cy += rowH + 14 * s;

        caption(cx, TR("Порт", "Port"));
        caption(cx + 170 * s, TR("Пароль", "Password"));
        cy += ch + 6 * s;
        enter |= w::TextBox("port", cx, cy, 150 * s, rowH, g_port, 5, false, true);
        enter |= w::TextBox("pass", cx + 170 * s, cy, cw - 170 * s, rowH, g_password, 32, true, false, TR("необязательно", "optional"));
        cy += rowH + 22 * s;
        if (g_tab == 1) {  // the host decides for the whole game; saved with the other settings when the server starts
            w::Checkbox("ff", cx, cy, TR("Урон по напарнику", "Friendly fire"), Cfg().friendlyFire);
            cy += lh * 0.8f + 22 * s;
            w::Checkbox("cheats", cx, cy, TR("Разрешить чит-коды", "Allow cheat codes"), Cfg().allowCheats);
            cy += lh * 0.8f + 22 * s;
        }

        // the main action: a big menu item in the middle, lit like the selected entry of the game's menus
        bool can = g_tab == 0 ? !Trim(Narrow(g_addr)).empty() : inGame || hostSlot != 0;
        if ((w::Button("go", cx, cy, cw, bh + 10 * s, g_tab == 0 ? TR("Подключиться", "Connect") : TR("Создать сервер", "Start the server"),
                       can, true, true) ||
             (enter && can)) &&
            ApplyNick()) {
            if (g_tab == 0) flow::Join(Trim(Narrow(g_addr)), PortValue(), Narrow(g_password));
            else flow::Host(inGame ? 0 : hostSlot, PortValue(), Narrow(g_password));
        }
        cy += bh + 10 * s + 14 * s;

        std::string error = !g_formError.empty() ? g_formError : flow::LastError();
        if (!error.empty()) para(error, kSaRed, FONT_NORMAL, true);
        else if (g_tab == 0 && inGame)
            para(TR("Текущая игра закроется. Вместо неё загрузится мир друга.",
                    "Your current game will close, and your friend's world will load instead."),
                 kSaMuted, FONT_SMALL, true);
        else if (g_tab == 1 && !inGame && !hostSlot)
            para(TR("В этом слоте нет сохранения.", "This slot has no save."), kSaMuted, FONT_SMALL, true);
        else if (g_tab == 1 && !inGame && hostSlot < 0)
            para(TR("Пролог вы пройдёте сами, а друг появится рядом, когда CJ сядет на велосипед.",
                    "You play the prologue alone, and your friend appears next to you when CJ gets on the bike."),
                 kSaMuted, FONT_SMALL, true);
    }

    // ---- footer: like "Back" on the game's pages
    g.Rect(cx, bodyBottom, cw, std::max(1.0f, s), kSaLine);
    if(g_streetBrowser){
        if(item("back-street",cx,by,TR("Назад","Back"))){if(g_privateEditor)g_privateEditor=false;else g_streetBrowser=false;}
    } else if (g_balanceEditor) {
        if (item("back-balances", cx, by, TR("Назад", "Back"))) g_balanceEditor = false;
    } else if (g_skinBrowser) {
        if (item("back-skins", cx, by, TR("Готово", "Done"))) g_skinBrowser = false;
    } else if (online || busy) {
        if (item("leave", cx, by, online ? TR("Отключиться", "Disconnect") : TR("Отмена", "Cancel"))) flow::Leave();
    }
    std::string close = online || busy || inGame ? TR("Закрыть", "Close") : TR("Одиночная игра", "Single player");
    if (item("close", cx + cw - g.TextWidth(FONT_MENU, close), by, close)) SetConnectMenuVisible(false);
    g.Text(FONT_SMALL, cx, fy,
           TR("{C1A1FF}F9{9E94B1}  окно     {C1A1FF}T{9E94B1}  чат     {C1A1FF}B{9E94B1}  голос     "
              "{C1A1FF}G{9E94B1}  сесть пассажиром     {C1A1FF}F2{9E94B1}  игроки",
              "{C1A1FF}F9{9E94B1}  window     {C1A1FF}T{9E94B1}  chat     {C1A1FF}B{9E94B1}  voice     "
              "{C1A1FF}G{9E94B1}  passenger     {C1A1FF}F2{9E94B1}  players"),
           kSaMuted, TEXT_SHADOW);
    w::EndFrame();
    w::DrawCursor();
    // Esc closes even when a text box is focused (handled in OnKeyDown)
}

}  // namespace coop::ui
