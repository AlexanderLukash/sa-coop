#include "entities/economy/Economy.h"
#include <algorithm>
#include <map>
#include <set>
#include "common/api/protocol/Protocol.h"
#include "common/economy/CashProjection.h"
#include "common/lib/util/Util.h"
#include "entities/session/Flow.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"

namespace coop {
namespace {
street::Snapshot g_snapshot;
bool g_have=false,g_started=false,g_initialRequested=false;
uint64_t g_received=0,g_sequence=0,g_syncAt=0;
economy::CashProjection g_projection;

void ProjectCash() {
    if(!g_have||!g_started||!core::IsPlayerReady()||!flow::InSharedWorld())return;
    int32_t projected=g_projection.Project(g_snapshot.cash);
    auto& info=game::PlayerInfos()[0];info.money=info.displayMoney=projected;
}
void BeginWorld() {
    if(!g_have||!core::IsPlayerReady()||!flow::InSharedWorld())return;
    int32_t seed=std::clamp(game::PlayerInfos()[0].money,0,(int32_t)economy::kMaximumCash);
    g_started=true;
    if(!g_snapshot.initialized&&!g_initialRequested){
        Writer request=EconomyRequest(street::ClientOp::Initialize);request.I32(seed);Net().Send(proto::CH_RELIABLE,request);
        g_initialRequested=true;
    }
    ProjectCash();
}
void CaptureNative(){
    if(!g_started||!core::IsPlayerReady()||!flow::InSharedWorld())return;
    int64_t delta=g_projection.Difference(game::PlayerInfos()[0].money);
    if(!delta)return;
    if(g_projection.pending.size()<16&&delta>=-10000000&&delta<=10000000){
        Writer w=EconomyRequest(street::ClientOp::CashChange);uint64_t sequence=g_sequence;w.I32((int32_t)delta);
        g_projection.Capture(sequence,(int32_t)delta);Net().Send(proto::CH_RELIABLE,w);
    }
    ProjectCash();
}
const char* ErrorText(street::Error error) {
    using street::Error;
    switch(error){
    case Error::Locked:return TR("Гонки отключены.","Racing is disabled.");
    case Error::Missing:return TR("Событие, игрок или запрос больше недоступен.","The event, player or request is no longer available.");
    case Error::NotOpen:return TR("Регистрация закрыта или действие уже выполнено.","Registration is closed or the action already completed.");
    case Error::Full:return TR("Все места в гонке заняты.","The race is full.");
    case Error::Funds:return TR("Недостаточно денег.","Not enough cash.");
    case Error::Vehicle:return TR("Нужен исправный автомобиль разрешённого класса; вы должны быть водителем.","You must drive a working car of an eligible class.");
    case Error::Location:return TR("Подъедьте к старту гонки.","Drive to the race start.");
    case Error::Busy:return TR("Вы уже участвуете в гонке или маршрут занят.","You are already racing or the route is occupied.");
    case Error::Invitation:return TR("Для этой гонки нужно приглашение.","This race requires an invitation.");
    case Error::Replay:return TR("Запрос устарел. Дождитесь обновления баланса.","The request is stale. Wait for the cash update.");
    case Error::Storage:return TR("Хранилище недоступно. Деньги и ставки не изменены.","Storage is unavailable. Cash and stakes were not changed.");
    default:return TR("Недопустимая сумма или достигнут лимит запросов.","Invalid amount or request limit reached.");
    }
}
class Economy : public Module {
public:
    const char* Name() const override{return "Economy";}
    void OnInstallHooks() override {
        Net().SetHandler(proto::S_STREET_RACING,[](uint8_t from,Reader& reader){
            if(from!=proto::kServerId)return;
            auto op=(street::ServerOp)reader.U8();
            if(op==street::ServerOp::Snapshot){
                street::Snapshot next;if(!next.Read(reader))return;
                if(g_have&&next.revision<g_snapshot.revision)return;
                CaptureNative();
                g_projection.Acknowledge(next.sequence);
                g_sequence=std::max(g_sequence,next.sequence);g_snapshot=std::move(next);g_have=true;g_received=NowMs();
                if(g_snapshot.initialized)g_initialRequested=true;
                ProjectCash();
            }else if(op==street::ServerOp::CashSet){
                auto account=reader.Str(64);int32_t cash=reader.I32();
                if(reader.Ok()&&!reader.Remaining()&&cash>=0&&cash<=economy::kMaximumCash)
                    ui::TheChat().AddSystem(Format(TR("Баланс %s установлен: $%d.","Balance for %s set to $%d."),account.c_str(),cash));
            }else if(op==street::ServerOp::Notice){
                auto error=(street::Error)reader.U8();if(!reader.Ok()||reader.Remaining()||error>street::Error::Storage)return;
                ui::TheChat().AddSystem(ErrorText(error));
                if(error==street::Error::Storage||error==street::Error::Replay){
                    g_projection.pending.clear();g_sequence=g_snapshot.sequence;g_initialRequested=false;ProjectCash();
                }
            }
        });
    }
    void OnSessionStart() override {
        g_snapshot={};g_have=g_started=g_initialRequested=false;g_received=g_sequence=g_syncAt=0;g_projection={};
    }
    void OnSessionEnd() override {g_have=g_started=false;g_projection.pending.clear();}
    void OnGameShutdown() override {g_started=false;g_projection.pending.clear();}
    void OnGameStarted() override {g_started=false;BeginWorld();}
    void OnPostProcess() override {
        if(!Net().Online())return;
        if(!g_have){if(NowMs()-g_syncAt>1000){Writer w(proto::C_STREET_RACING);w.U8((uint8_t)street::ClientOp::Sync);Net().Send(proto::CH_RELIABLE,w);g_syncAt=NowMs();}return;}
        if(!core::IsPlayerReady()||!flow::InSharedWorld())return;
        if(!g_started){BeginWorld();return;}
        CaptureNative();
    }
};
struct Reg{Reg(){static Economy module;core::RegisterModule(&module);}}g_reg;
}
const street::Snapshot* EconomySnapshot(){return g_have?&g_snapshot:nullptr;}
uint64_t EconomyReceivedMs(){return g_received;}
bool CashAuthorityOnline(){return Net().Online();}
uint64_t NextCashSequence(){return g_have?++g_sequence:0;}
Writer EconomyRequest(street::ClientOp operation){Writer w(proto::C_STREET_RACING);w.U8((uint8_t)operation);w.U64(NextCashSequence());return w;}
bool SetServerCash(uint8_t player,int32_t amount){
    if(!g_have||!Net().IsHost()||amount<0||amount>economy::kMaximumCash)return false;
    if(player!=Net().MyId()&&!Net().Peer(player))return false;
    Writer w=EconomyRequest(street::ClientOp::SetCash);w.U8(player);w.Str(ToLower(Net().PeerName(player)),64);w.I32(amount);Net().Send(proto::CH_RELIABLE,w);return true;
}
void RefreshAuthoritativeCash(){ProjectCash();}
int64_t NativeCashFlow(){
    return g_projection.flow+(g_started&&core::IsPlayerReady()&&flow::InSharedWorld()?g_projection.Difference(game::PlayerInfos()[0].money):0);
}
}
