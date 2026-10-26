#include "features/street-racing/StreetRacing.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include "common/api/protocol/Protocol.h"
#include "common/lib/util/Util.h"
#include "entities/economy/Economy.h"
#include "entities/player/Players.h"
#include "entities/session/Flow.h"
#include "entities/vehicle/Vehicles.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"
#include "shared/ui/renderer/Renderer.h"

namespace coop {
namespace {
using namespace game;
using namespace street;
using game::script::Cmd;
constexpr uint8_t STATUS_WRECKED = 5;
uint64_t g_worldAt=0,g_observeAt=0,g_gridEvent=0,g_markRevision=0;
uint32_t g_frame=0;
int g_frozen=-1,g_checkpoint=-1,g_checkpointBlip=-1;
CVehicle* g_frozenPointer=nullptr;
uint64_t g_checkpointEvent=0;
uint16_t g_next=0;
std::map<uint64_t,int> g_eventBlips;
std::vector<int> g_preview;
std::set<uint64_t> g_announced,g_results;

bool Ready(){return Net().Online()&&core::IsPlayerReady()&&flow::InSharedWorld()&&EconomySnapshot();}
const ParticipantView* Me(const EventView& e){
    const auto* state=EconomySnapshot();if(!state)return nullptr;
    for(const auto& p:e.participants)if(p.account==state->account)return &p;
    return nullptr;
}
void ReleaseCar(){
    if(g_frozen>=0&&core::IsPlayerReady()&&GetVehicle(g_frozen)==g_frozenPointer)Cmd(0x0519).I(g_frozen).I(0).Run();
    g_frozen=-1;g_frozenPointer=nullptr;
}
void ClearCheckpoint(){
    if(core::IsPlayerReady()){
        if(g_checkpoint!=-1)Cmd(0x06D6).I(g_checkpoint).Run();
        if(g_checkpointBlip!=-1)Cmd(0x0164).I(g_checkpointBlip).Run();
    }
    g_checkpoint=g_checkpointBlip=-1;g_checkpointEvent=0;g_next=0;
}
void ClearPreview(){if(core::IsPlayerReady())for(int blip:g_preview)Cmd(0x0164).I(blip).Run();g_preview.clear();}
void ClearWorld(){
    ReleaseCar();ClearCheckpoint();ClearPreview();
    if(core::IsPlayerReady())for(auto [id,blip]:g_eventBlips)Cmd(0x0164).I(blip).Run();
    g_eventBlips.clear();g_gridEvent=g_markRevision=0;
}
void WriteObservation(Writer& w,uint8_t id,CPed* ped,const Vec3& position,uint16_t vehicle,uint16_t model,bool driver,bool available,uint8_t area,bool alive){
    (void)ped;w.U8(id);w.Vec(position);w.U16(vehicle);w.U16(model);w.Bool(driver);w.Bool(alive);w.Bool(available);w.U8(area);
}
void Witness(uint64_t now){
    if(!Net().IsHost())return;
    bool freeWorld=!core::IsOnMission()&&!Ref<uint8_t>(0xB5F851); // CCutsceneMgr::ms_running
    if(now-g_worldAt>=1000){
        Writer w(proto::C_STREET_RACING);w.U8((uint8_t)ClientOp::World);
        // Verified in supported main.scm: CPRACE race 0 registers CESAR_1 and
        // sets global byte offset 9200 only after its first victory.
        w.Bool(Ref<int32_t>(addr::ScriptSpace+9200)>0);
        w.U8((uint8_t)std::clamp(Ref<int32_t>(0xB79000+(181-120)*4),0,4));w.Bool(freeWorld);
        Net().Send(proto::CH_RELIABLE,w);g_worldAt=now;
    }
    if(!freeWorld||!EconomySnapshot()->unlocked)return;
    if(now-g_observeAt<250)return;g_observeAt=now;
    Writer samples;uint8_t count=0;
    CPlayerPed* me=FindPlayerPed(0);CVehicle* own=me->VehicleIfIn();
    NetVehicle* ownNet=own?Vehicles::Instance().EnsureNetworked(own):nullptr;
    CVector position=own?own->GetPosition():me->GetPosition();
    WriteObservation(samples,Net().MyId(),me,{position.x,position.y,position.z},ownNet?ownNet->netId:0,
        own?own->ModelIndex():0,own&&own->Driver()==me,freeWorld,(uint8_t)me->AreaCode(),me->Health()>0&&(!own||(own->Health()>0&&own->Status()!=STATUS_WRECKED)));++count;
    for(const auto& [id,player]:Players::Instance().All()){
        if(!player.hasState||!player.ped||now-player.stateRecvMs>2000)continue;
        NetVehicle* nv=Vehicles::Instance().Find(player.vehNetId);
        bool driver=player.mode==RemotePlayer::Mode::Driver&&nv&&Vehicles::Alive(*nv)&&nv->veh->Driver()==player.ped;
        const CVector& p=player.state.pos;
        WriteObservation(samples,id,player.ped,{p.x,p.y,p.z},driver?nv->netId:0,driver?nv->model:0,driver,freeWorld,
            (uint8_t)player.state.area,player.state.health>0&&!(player.state.flags&OnFootState::F_DEAD)&&(!driver||(nv->veh->Health()>0&&nv->veh->Status()!=STATUS_WRECKED)));++count;
    }
    Writer w(proto::C_STREET_RACING);w.U8((uint8_t)ClientOp::Observe);w.U32(++g_frame);w.U8(count);w.Raw(samples.Data(),samples.Size());Net().Send(proto::CH_RELIABLE,w);
}
void Checkpoint(const EventView& e,const ParticipantView& racer){
    const Route& route=*FindRoute(e.route);
    if(g_checkpointEvent==e.id&&g_next==racer.next)return;
    ClearCheckpoint();
    if(racer.next>=route.checkpoints.size())return;
    Vec3 p=route.checkpoints[racer.next],next=route.checkpoints[std::min<size_t>(racer.next+1,route.checkpoints.size()-1)];
    Cmd(0x06D5).I(racer.next+1==route.checkpoints.size()?1:0).F(p.x).F(p.y).F(p.z).F(next.x).F(next.y).F(next.z).F(12).Out(&g_checkpoint).Run();
    Cmd(0x018A).F(p.x).F(p.y).F(p.z).Out(&g_checkpointBlip).Run();
    Cmd(0x0165).I(g_checkpointBlip).I(1).Run();g_checkpointEvent=e.id;g_next=racer.next;
}
void Prepare(const EventView& e,const ParticipantView& racer){
    if(g_gridEvent==e.id)return;g_gridEvent=e.id;
    CPlayerPed* me=FindPlayerPed(0);CVehicle* car=me->VehicleIfIn();
    if(!car||car->Driver()!=me||core::IsOnMission()||!Eligible(*FindRoute(e.route),car->ModelIndex())){LeaveStreetRace(e.id);return;}
    const Route& route=*FindRoute(e.route);Vec3 spot=GridSpot(route,racer.grid);
    bool ground=false;float z=Call<0x5696C0,float>(spot.x,spot.y,spot.z+10,&ground,(CEntity**)nullptr);
    if(!ground||std::abs(z-spot.z)>6){LeaveStreetRace(e.id);return;}
    ReleaseCar();g_frozen=GetVehicleRef(car);g_frozenPointer=car;
    Cmd(0x00AB).I(g_frozen).F(spot.x).F(spot.y).F(z+0.9f).Run();
    Vec3 a=route.checkpoints[0],b=route.checkpoints[1];float heading=std::atan2(-(b.x-a.x),b.y-a.y)*57.29578f;
    Cmd(0x0175).I(g_frozen).F(heading).Run();car->MoveSpeed()={};Cmd(0x0519).I(g_frozen).I(1).Run();
}
void UpdateWorld(){
    const auto& state=*EconomySnapshot();
    bool racing=false,preparing=false;
    for(const auto& e:state.events){
        const auto* racer=Me(e);
        if(e.phase==Phase::Countdown&&racer){preparing=true;Prepare(e,*racer);}
        if(e.phase==Phase::Racing&&racer&&racer->outcome==Outcome::Racing){racing=true;Checkpoint(e,*racer);}
        if(e.phase==Phase::Settled&&racer&&g_results.insert(e.id).second){
            if(e.reason==Reason::Completed||e.reason==Reason::NoFinishers)
                ui::TheChat().AddSystem(Format(TR("Гонка: место %u, выплата $%u.","Race result: place %u, payout $%u."),racer->place,racer->paid));
            else ui::TheChat().AddSystem(TR("Гонка отменена. Ставки возвращены сервером.","The race was cancelled. Stakes were refunded by the server."));
        }
    }
    if(!preparing)ReleaseCar();if(!racing)ClearCheckpoint();
    if(g_markRevision==state.revision)return;g_markRevision=state.revision;
    std::set<uint64_t> wanted;
    for(const auto& e:state.events)if(e.phase<=Phase::Registration&&state.unlocked&&state.available){
        wanted.insert(e.id);
        if(!g_eventBlips.count(e.id)){
            Vec3 p=FindRoute(e.route)->checkpoints.front();int blip=-1;
            Cmd(0x018A).F(p.x).F(p.y).F(p.z).Out(&blip).Run();Cmd(0x0165).I(blip).I(3).Run();g_eventBlips[e.id]=blip;
        }
        if(g_announced.insert(e.id).second)
            ui::TheChat().AddSystem(Format(TR("Уличная гонка: %s, взнос $%u. Регистрация в F9.","Street race: %s, entry $%u. Register in F9."),FindRoute(e.route)->name,e.fee));
    }
    for(auto it=g_eventBlips.begin();it!=g_eventBlips.end();)if(!wanted.count(it->first)){Cmd(0x0164).I(it->second).Run();it=g_eventBlips.erase(it);}else ++it;
}
class StreetRacing : public Module {
public:
    const char* Name() const override{return "StreetRacing";}
    void OnSessionStart() override{g_worldAt=g_observeAt=0;g_frame=0;g_announced.clear();g_results.clear();}
    void OnSessionEnd() override{ClearWorld();g_announced.clear();g_results.clear();}
    void OnGameShutdown() override{ClearWorld();if(Net().IsHost()){Writer w(proto::C_STREET_RACING);w.U8((uint8_t)ClientOp::World);w.Bool(false);w.U8(0);w.Bool(false);Net().Send(proto::CH_RELIABLE,w);}}
    void OnPostProcess() override{if(Ready()){Witness(NowMs());UpdateWorld();}}
    void OnRender() override{
        if(!Ready())return;
        const auto& state=*EconomySnapshot();auto& g=ui::Gfx();float scale=g.Scale();
        for(const auto& e:state.events)if(const auto* racer=Me(e);racer&&e.phase>=Phase::Countdown&&e.phase<=Phase::Racing){
            uint64_t age=NowMs()-EconomyReceivedMs();const Route& route=*FindRoute(e.route);std::string text;
            if(e.phase==Phase::Countdown)text=Format(TR("%s · старт через %u", "%s · starts in %u"),route.name,(unsigned)((e.remaining>age?e.remaining-age:0)+999)/1000);
            else if(racer->outcome==Outcome::Finished)text=TR("Финиш! Ждём остальных гонщиков.","Finished! Waiting for the other racers.");
            else if(racer->outcome==Outcome::Forfeit)text=TR("Гонка не завершена. Ставка потеряна.","Did not finish. The stake is forfeited.");
            else text=Format(TR("%s · точка %u/%u · %.1f с", "%s · checkpoint %u/%u · %.1f s"),route.name,
                racer->next,(unsigned)route.checkpoints.size()-1,(e.elapsed+age)/1000.0);
            g.Text(ui::FONT_NORMAL,g.Width()*0.5f,120*scale,text,0xFFC1A1FF,ui::TEXT_CENTER|ui::TEXT_SHADOW);break;
        }
    }
};
struct Reg{Reg(){static StreetRacing module;core::RegisterModule(&module);}}g_reg;
}
bool JoinStreetRace(uint64_t event){if(!Ready()||core::IsOnMission())return false;Writer w=EconomyRequest(ClientOp::Join);w.U64(event);Net().Send(proto::CH_RELIABLE,w);return true;}
bool LeaveStreetRace(uint64_t event){if(!Ready())return false;Writer w=EconomyRequest(ClientOp::Leave);w.U64(event);Net().Send(proto::CH_RELIABLE,w);return true;}
bool CancelStreetRace(uint64_t event){if(!Ready())return false;Writer w=EconomyRequest(ClientOp::Cancel);w.U64(event);Net().Send(proto::CH_RELIABLE,w);return true;}
bool CreateStreetRace(uint16_t route,uint32_t stake,uint8_t capacity,const std::vector<uint8_t>& invited){
    if(!Ready()||invited.size()>proto::kMaxPlayers)return false;
    for(auto id:invited)if(id!=Net().MyId()&&!Net().Peer(id))return false;
    Writer w=EconomyRequest(ClientOp::Create);w.U16(route);w.U32(stake);w.U8(capacity);w.U8((uint8_t)invited.size());
    for(auto id:invited){w.U8(id);w.Str(ToLower(Net().PeerName(id)),64);}Net().Send(proto::CH_RELIABLE,w);return true;
}
bool CreateStreetTestRace(){
    if(!Ready()||!Net().IsHost()||Net().Peers().empty())return false;
    const auto* state=EconomySnapshot();
    for(const auto& route:Routes())if(Available(route,state->unlocked,state->city)){
        std::vector<uint8_t> invited;invited.reserve(Net().Peers().size());
        for(const auto& [id,peer]:Net().Peers())invited.push_back(id);
        return CreateStreetRace(route.id,0,(uint8_t)(invited.size()+1),invited);
    }
    return false;
}
void PreviewStreetRoute(uint64_t id){
    if(!Ready())return;ClearPreview();
    for(const auto& e:EconomySnapshot()->events)if(e.id==id){const auto& route=*FindRoute(e.route);
        for(size_t i=0;i<route.checkpoints.size();++i){auto p=route.checkpoints[i];int blip=-1;Cmd(0x018A).F(p.x).F(p.y).F(p.z).Out(&blip).Run();
            Cmd(0x0165).I(blip).I(i==0?1:i+1==route.checkpoints.size()?0:3).Run();g_preview.push_back(blip);}
        ui::TheChat().AddSystem(TR("Маршрут отмечен на карте: старт, синие точки, финиш.","Route marked on the map: start, blue checkpoints, finish."));break;
    }
}
}
