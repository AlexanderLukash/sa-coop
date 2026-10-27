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
                Check(c.states[0].cash==10000&&c.states[1].cash==10000,"restart lost cash");
                Check(c.states[0].events.empty(),"restart restored races");
                std::puts("Disabled racing: cash survives restart: OK");
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
                c.Cash(1,10000);c.World(0);c.Pump();
                Check(!c.states[0].unlocked,"disabled racing unlocked");
                Writer create=c.Request(0,ClientOp::Create);create.U16(1);create.U32(100);create.U8(2);create.U8(0);c.Ack(0,create);
                Check(!c.errors[0].empty()&&c.errors[0].back()==Error::Locked,"race creation not rejected");
                c.Pump(1500);
                Check(c.states[0].events.empty()&&c.states[1].events.empty(),"disabled racing created events");
                Check(c.states[0].cash==10000&&c.states[1].cash==10000,"disabled race changed balances");
                std::puts("Disabled racing: no events, rejected creation, income approvals and cash: OK");
            }
        }
        enet_deinitialize();return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
