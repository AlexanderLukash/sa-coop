// Real server integration: host authority, GTA cash, private races and crash recovery.
#include <enet/enet.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include "common/api/protocol/Protocol.h"
#include "common/config/version/Version.h"
#include "common/racing/RaceProtocol.h"
using namespace coop;
using namespace coop::proto;
using namespace coop::street;
using Clock=std::chrono::steady_clock;
void Check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
uint64_t Ms(){return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
struct Clients {
    std::array<ENetHost*,3> hosts{};std::array<ENetPeer*,3> peers{};
    std::array<bool,3> welcomed{},connected{},have{};std::array<uint8_t,3> ids{},hostIds{};
    std::array<Snapshot,3> states;
    std::array<std::vector<Error>,3> errors;
    std::array<Vec3,3> positions{};
    ENetAddress address{};bool witness=false;uint32_t frame=0;uint64_t lastWorld=0,lastObservation=0;
    explicit Clients(int port){enet_address_set_host(&address,"127.0.0.1");address.port=(enet_uint16)port;}
    ~Clients(){for(auto* host:hosts)if(host)enet_host_destroy(host);}
    void Connect(int player){
        if(hosts[player])enet_host_destroy(hosts[player]);welcomed[player]=have[player]=false;
        hosts[player]=enet_host_create(nullptr,1,CH_COUNT,0,0);Check(hosts[player],"client creation");
        enet_host_compress_with_range_coder(hosts[player]);peers[player]=enet_host_connect(hosts[player],&address,CH_COUNT,0);
        connected[player]=true;
    }
    void Send(int player,const Writer& w){enet_peer_send(peers[player],CH_RELIABLE,enet_packet_create(w.Data(),w.Size(),ENET_PACKET_FLAG_RELIABLE));enet_host_flush(hosts[player]);}
    Writer Request(int player,ClientOp operation){Writer w(C_STREET_RACING);w.U8((uint8_t)operation);w.U64(states[player].sequence+1);return w;}
    void Status(int player){Writer w(C_STATUS);w.U8(STATUS_INGAME);Send(player,w);}
    void World(int player){Writer w(C_STREET_RACING);w.U8((uint8_t)ClientOp::World);w.Bool(true);w.U8(0);w.Bool(true);Send(player,w);}
    void Observations(){
        Writer w(C_STREET_RACING);w.U8((uint8_t)ClientOp::Observe);w.U32(++frame);unsigned count=0;
        for(unsigned i=0;i<3;++i)if(connected[i]&&welcomed[i])++count;w.U8((uint8_t)count);
        for(unsigned i=0;i<3;++i)if(connected[i]&&welcomed[i]){
            w.U8(ids[i]);w.Vec(positions[i]);w.U16((uint16_t)(0x1001+i));w.U16(401);w.Bool(true);w.Bool(true);w.Bool(true);w.U8(0);
        }Send(0,w);
    }
    void Poll(){
        if(witness&&connected[0]){uint64_t now=Ms();if(now-lastWorld>400){World(0);lastWorld=now;}if(now-lastObservation>100){Observations();lastObservation=now;}}
        for(int i=0;i<3;++i)if(hosts[i]){
            ENetEvent ev;while(enet_host_service(hosts[i],&ev,0)>0){
                if(ev.type==ENET_EVENT_TYPE_CONNECT){Hello h;h.protocol=kProtocolVersion;h.version=COOP_VERSION_STR;h.name="Racer"+std::to_string(i);h.wantsHost=i!=1;h.exeSize=kSupportedExeSize;
                    Writer w(C_HELLO);h.Write(w);Send(i,w);
                }else if(ev.type==ENET_EVENT_TYPE_DISCONNECT)connected[i]=false;
                else if(ev.type==ENET_EVENT_TYPE_RECEIVE){Reader r(ev.packet->data,ev.packet->dataLength);uint8_t id=r.U8();
                    if(id==S_WELCOME){Welcome welcome;Check(welcome.Read(r),"welcome");ids[i]=welcome.yourId;hostIds[i]=welcome.hostId;welcomed[i]=true;}
                    else if(id==S_REJECT)throw std::runtime_error("unexpected connection rejection");
                    else if(id==S_STREET_RACING){auto op=(ServerOp)r.U8();if(op==ServerOp::Snapshot){Check(states[i].Read(r),"snapshot format");have[i]=true;}
                        else if(op==ServerOp::Notice)errors[i].push_back((Error)r.U8());}
                    enet_packet_destroy(ev.packet);
                }
            }
        }
    }
    template<class F>void Until(F done,const char* why,uint64_t timeout=10000){uint64_t end=Ms()+timeout;
        while(!done()&&Ms()<end){Poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));}Check(done(),why);}
    void Pump(unsigned ms=250){uint64_t end=Ms()+ms;while(Ms()<end){Poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
    void Ack(int player,const Writer& w){uint64_t sequence=states[player].sequence+1;Send(player,w);Until([&]{return states[player].sequence==sequence;},"transaction acknowledgement");}
    const EventView* Event(int player,uint64_t event) const {for(const auto& e:states[player].events)if(e.id==event)return &e;return nullptr;}
    void Cash(int target,int amount){Writer w=Request(0,ClientOp::SetCash);w.U8(ids[target]);w.Str("racer"+std::to_string(target));w.I32(amount);Ack(0,w);Until([&]{return states[target].cash==amount;},"administrative cash");}
    uint64_t Create(uint16_t route){Writer w=Request(0,ClientOp::Create);w.U16(route);w.U32(100);w.U8(2);w.U8(1);w.U8(ids[1]);w.Str("racer1");Ack(0,w);
        uint64_t id=0;Until([&]{for(const auto& e:states[0].events)if(e.route==route&&e.phase==Phase::Registration)id=e.id;return id!=0;},"private registration");return id;}
    void Join(int player,uint64_t event){Writer w=Request(player,ClientOp::Join);w.U64(event);Ack(player,w);}
};
Vec3 Along(const Route& r,float distance){for(size_t i=1;i<r.checkpoints.size();++i){auto a=r.checkpoints[i-1],b=r.checkpoints[i];float length=Distance(a,b);
    if(distance<=length){float t=distance/length;return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}distance-=length;}return r.checkpoints.back();}
int main(int argc,char** argv){
    try{
        Check(argc==3,"usage: street-network-test PORT race|recover");Check(enet_initialize()==0,"ENet init");
        {
            Clients c(std::atoi(argv[1]));bool recover=std::string(argv[2])=="recover";
            c.Connect(2);c.Until([&]{return c.welcomed[2]&&c.have[2];},"outsider handshake");
            Check(c.hostIds[2]==255,"wantsHost bypassed reserved host identity");
            c.Connect(0);c.Until([&]{return c.welcomed[0]&&c.have[0];},"host handshake");c.Status(0);
            c.Connect(1);c.Until([&]{return c.welcomed[1]&&c.have[1];},"guest handshake");c.Status(1);c.Status(2);c.Pump();
            if(recover){
                Check(c.states[0].cash==9900&&c.states[1].cash==10100,"restart lost or duplicated escrow refund/payout");
                bool refunded=false;for(const auto& event:c.states[0].events)if(event.reason==Reason::Restart&&event.phase==Phase::Settled)refunded=true;
                Check(refunded,"unfinished race was not cancelled on restart");
                Check(c.states[0].statistics.races==1&&c.states[1].statistics.wins==1,"race statistics not persisted exactly once");
                std::puts("Street network: restart refunds active escrow once and preserves cash/statistics: OK");
            }else{
                c.World(2);c.Pump();Check(!c.states[0].unlocked,"non-host unlocked races");
                Writer fake=c.Request(1,ClientOp::Initialize);fake.I32(999999999);c.Ack(1,fake);Check(c.states[1].cash==0,"guest seeded arbitrary cash");
                Writer initialize=c.Request(0,ClientOp::Initialize);initialize.I32(10000);c.Ack(0,initialize);Check(c.states[0].cash==10000,"host seed failed");
                Writer income=c.Request(1,ClientOp::CashChange);income.I32(500);c.Ack(1,income);
                c.Until([&]{return c.states[0].credits.size()==1;},"income approval request");Check(c.states[1].cash==0,"unverified income was credited");
                auto credit=c.states[0].credits.front().id;Writer approval=c.Request(0,ClientOp::DecideCredit);approval.U64(credit);approval.Bool(true);c.Ack(0,approval);
                c.Until([&]{return c.states[1].cash==500;},"approved GTA cash");c.Send(0,approval);c.Pump();Check(c.states[1].cash==500,"duplicate income approval minted cash");
                Writer spending=c.Request(1,ClientOp::CashChange);spending.I32(-200);c.Ack(1,spending);Check(c.states[1].cash==300,"native spending not reconciled");
                Writer overdraft=c.Request(1,ClientOp::CashChange);overdraft.I32(-301);c.Ack(1,overdraft);Check(c.states[1].cash==300,"overdraft applied");
                Writer fakeIncome(G_MISSION_REWARD);fakeIncome.U64(123);fakeIncome.I32(100000);c.Send(1,fakeIncome);c.Pump();
                Check(c.states[1].cash==300,"guest forged a shared mission reward");
                Writer menu(C_STATUS);menu.U8(STATUS_MENU);c.Send(2,menu);c.Pump(100);
                Writer award(G_MISSION_REWARD);award.U64(c.states[0].sequence+1);award.I32(100);c.Send(0,award);
                c.Until([&]{return c.states[1].cash==400;},"authoritative shared mission cash");c.Send(0,award);c.Pump();
                Check(c.states[1].cash==400&&c.states[2].cash==0,"mission award repeated or credited a loading spectator");c.Status(2);
                c.Cash(1,10000);c.World(0);c.Until([&]{return c.states[0].unlocked;},"host world progression");
                Writer locked=c.Request(0,ClientOp::Create);locked.U16(5);locked.U32(100);locked.U8(2);locked.U8(0);c.Ack(0,locked);
                Check(c.states[0].events.empty(),"locked region selected");
                c.positions[0]=c.positions[1]=FindRoute(1)->checkpoints.front();c.witness=true;c.Pump();
                uint64_t event=c.Create(1);Check(c.states[2].events.empty(),"private race leaked to outsider");
                c.Join(0,event);c.Join(1,event);Check(c.states[0].cash==9900&&c.states[1].cash==9900,"stakes not escrowed");
                Writer duplicate=c.Request(1,ClientOp::Join);duplicate.U64(event);c.Ack(1,duplicate);Check(c.states[1].cash==9900,"duplicate registration charged twice");
                c.Join(2,event);Check(c.states[2].cash==0,"outsider wagered in a private event");
                c.Until([&]{return c.Event(0,event)&&c.Event(0,event)->phase==Phase::Countdown;},"countdown",12000);
                for(unsigned i=0;i<2;++i){const auto* view=c.Event(i,event);for(const auto& p:view->participants)if(p.account=="racer"+std::to_string(i))c.positions[i]=GridSpot(*FindRoute(1),p.grid);}
                c.Until([&]{return c.Event(0,event)&&c.Event(0,event)->phase==Phase::Racing;},"race start",6000);
                Writer fakeFinish(C_STREET_RACING);fakeFinish.U8((uint8_t)ClientOp::Observe);fakeFinish.U32(999999);fakeFinish.U8(1);
                fakeFinish.U8(c.ids[0]);fakeFinish.Vec(FindRoute(1)->checkpoints.back());fakeFinish.U16(0x1001);fakeFinish.U16(401);
                fakeFinish.Bool(true);fakeFinish.Bool(true);fakeFinish.Bool(true);fakeFinish.U8(0);c.Send(2,fakeFinish);c.Pump(150);
                Check(c.Event(0,event)->participants.front().next==1,"non-host forged checkpoint progress");
                uint64_t start=Ms();bool guestLeft=false;
                c.Until([&]{
                    float seconds=(Ms()-start)/1000.0f;c.positions[0]=Along(*FindRoute(1),seconds*55);c.positions[1]=Along(*FindRoute(1),seconds*70);
                    const auto* view=c.Event(0,event);
                    if(view&&!guestLeft)for(const auto& p:view->participants)if(p.account=="racer1"&&p.outcome==Outcome::Finished){
                        enet_peer_disconnect(c.peers[1],1);enet_host_flush(c.hosts[1]);guestLeft=true;}
                    return view&&view->phase==Phase::Settled;
                },"authoritative finish and settlement",35000);
                Check(guestLeft&&c.states[0].cash==9900,"offline winner settlement failed");
                c.Connect(1);c.Until([&]{return c.welcomed[1]&&c.have[1];},"winner reconnect");c.Status(1);
                Check(c.states[1].cash==10100&&c.states[1].statistics.wins==1,"offline winner did not receive unique payout");
                // Client-controlled profile money must never overwrite an existing cash account.
                Writer profile;profile.U32(0x46504353);profile.U16(6);profile.I32(999999999);std::vector<uint8_t> padding(4096);profile.Raw(padding.data(),padding.size());
                Writer save(C_PROFILE_SAVE);save.Bytes(profile.Data(),profile.Size());c.Send(1,save);c.Pump();
                Writer sync(C_STREET_RACING);sync.U8((uint8_t)ClientOp::Sync);c.Send(1,sync);c.Pump();Check(c.states[1].cash==10100,"profile balance overwrote ledger");
                // Leave durable reservations in an active event for the crash/restart test.
                c.positions[0]=c.positions[1]=FindRoute(2)->checkpoints.front();c.Pump(150);
                uint64_t reserved=c.Create(2);c.Join(0,reserved);c.Join(1,reserved);
                Check(c.states[0].cash==9800&&c.states[1].cash==10000,"recovery reservations missing");
                std::puts("Street network: host identity, income approvals, spending, private visibility, escrow, offline payout and profile isolation: OK");
            }
        }
        enet_deinitialize();return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
