#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include "common/racing/RaceProtocol.h"
#include "common/economy/CashProjection.h"
#include "common/racing/LegacyBlips.h"
using namespace coop;
using namespace coop::street;

namespace {
Observation Sample(Vec3 p,uint16_t vehicle) {Observation s;s.position=p;s.vehicle=vehicle;s.model=401;s.driver=true;return s;}
int64_t Money(const RaceEngine& e,const char* key){return e.cash.Find(key)->cash;}
int64_t Total(const RaceEngine& e){int64_t n=0;for(const auto& [key,a]:e.cash.accounts)n+=a.cash;for(const auto& [id,event]:e.events)for(const auto& p:event.participants)n+=p.stake;return n;}
struct Fixture {
    RaceEngine e{77};uint64_t now=100000,id=0;
    Fixture(){
        e.cash.Open("host",10000,true);e.cash.Open("guest",10000,true);e.cash.Open("other",10000,true);e.cash.Open("fourth",10000,true);
        e.settings.registrationMs=1000;e.settings.countdownMs=1000;e.settings.maximumRaceMs=60000;e.settings.afterFirstFinishMs=3000;
        e.World(true,0,true,now);
        assert(e.Create("host",1,100,4,{"guest","other"},now,id)==Error::None);e.Tick(now);
    }
    void JoinBoth(){
        auto p=FindRoute(1)->checkpoints.front();e.Observe("host",Sample(p,100),now);e.Observe("guest",Sample(p,200),now);
        assert(e.Join("host",id,now)==Error::None);assert(e.Join("guest",id,now)==Error::None);
    }
    void Start(){
        JoinBoth();now=e.Find(id)->deadline;
        auto p=FindRoute(1)->checkpoints.front();e.Observe("host",Sample(p,100),now);e.Observe("guest",Sample(p,200),now);e.Tick(now);
        assert(e.Find(id)->phase==Phase::Countdown);now=e.Find(id)->deadline;
        e.Observe("host",Sample(GridSpot(*FindRoute(1),0),100),now);e.Observe("guest",Sample(GridSpot(*FindRoute(1),1),200),now);e.Tick(now);
        assert(e.Find(id)->phase==Phase::Racing);
    }
};
Vec3 Along(const Route& r,float distance){
    for(size_t i=1;i<r.checkpoints.size();++i){Vec3 a=r.checkpoints[i-1],b=r.checkpoints[i];float length=Distance(a,b);
        if(distance<=length){float t=distance/length;return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}distance-=length;
    }return r.checkpoints.back();
}
void Finish(Fixture& f,float speedHost=45,float speedGuest=40){
    uint64_t start=f.e.Find(f.id)->started;
    for(unsigned step=1;step<240&&f.e.Find(f.id)->phase<Phase::Settled;++step){
        f.now=start+step*250;float seconds=step*0.25f;const Route& route=*FindRoute(1);
        f.e.Observe("host",Sample(Along(route,seconds*speedHost),100),f.now);
        f.e.Observe("guest",Sample(Along(route,seconds*speedGuest),200),f.now);f.e.Tick(f.now);
    }
    assert(f.e.Find(f.id)->phase==Phase::Settled);
}
RaceEngine Restore(const RaceEngine& e){Writer w;e.Write(w);Reader r(w.Data(),w.Size());RaceEngine restored;assert(restored.Read(r));return restored;}
}
int main(){
    {
        economy::CashProjection p;
        assert(p.Project(1000)==1000&&p.Difference(950)==-50);
        p.Capture(1,-50);assert(p.Project(1000)==950&&p.flow==-50);
        assert(p.Project(1500)==1450&&p.flow==-50); // server/admin income while a native debit is in flight
        p.Acknowledge(1);assert(p.Project(1450)==1450&&p.flow==-50); // no double spending on acknowledgement
        p.Capture(2,200);assert(p.Project(1450)==1650&&p.flow==150); // earned cash is immediately spendable
        p.Acknowledge(2);assert(p.Project(1650)==1650&&p.flow==150);
        assert(p.Project(1750)==1750&&p.flow==150); // a server escrow refund is not shared as native mission income
        p.Capture(3,-2000);assert(p.Project(1750)==0);p.Acknowledge(3);assert(p.Project(1750)==1750); // rejected debit restores cash
    }
    {
        for(const auto& route:Routes())for(const auto& p:route.checkpoints){
            for(uint32_t colour:{0u,1u,3u})assert(LegacyRaceBlip(true,4,0,colour,p));
            assert(!LegacyRaceBlip(false,4,0,3,p));
            assert(!LegacyRaceBlip(true,5,0,3,p)); // mission contacts
            assert(!LegacyRaceBlip(true,4,41,3,p)); // player waypoint
            assert(!LegacyRaceBlip(true,4,35,3,p)); // save icon
            assert(!LegacyRaceBlip(true,4,0,2,p)); // other colour
            assert(!LegacyRaceBlip(true,4,0,3,{p.x+1,p.y,p.z}));
        }
        assert(!LegacyRaceBlip(true,4,0,3,{0,0,0}));
    }
    {
        Fixture f;f.JoinBoth();assert(f.e.cash.ProposeIncome("guest",250)==economy::CashError::None);
        auto migrated=Restore(f.e);migrated.Recover(100);
        assert(migrated.cash.CreditPendingIncome()==economy::CashError::None);
        assert(migrated.cash.credits.empty()&&Money(migrated,"guest")==10250&&Total(migrated)==40250);
        auto restarted=Restore(migrated);restarted.Recover(200);
        assert(restarted.cash.CreditPendingIncome()==economy::CashError::None);
        assert(Money(restarted,"guest")==10250&&Total(restarted)==40250);
    }
    {
        economy::CashProjection p;assert(p.Project(100)==100);
        p.Capture(1,500);assert(p.Project(100)==600); // pickup can be spent before server ACK
        p.Capture(2,-200);assert(p.Project(100)==400);
        p.Acknowledge(1);assert(p.Project(600)==400); // credit ACK keeps the pending purchase
        p.Acknowledge(2);assert(p.Project(400)==400&&p.flow==300);
    }
    // Catalog gates are finite, unique, ordered and stay inside their declared region.
    std::set<uint16_t> ids;
    for(const auto& r:Routes()){
        assert(ids.insert(r.id).second&&r.checkpoints.size()>=2&&r.Length()>100&&r.minimum>=2&&r.maximum<=8);
        assert(!Available(r,false,4));assert(Available(r,true,r.cityLevel));
        if(r.cityLevel)assert(!Available(r,true,r.cityLevel-1));
        assert(Eligible(r,401)&&!Eligible(r,432)&&!Eligible(r,520)&&!Eligible(r,581));
        for(auto p:r.checkpoints){assert(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&p.z<500);
            if(r.region==Region::SanFierro)assert(p.x< -1500);
            if(r.region==Region::LasVenturas)assert(p.x>1000&&p.y>800);}
    }
    assert(Available(*FindRoute(5),true,1)&&!Available(*FindRoute(6),true,1));
    {
        Fixture f;f.JoinBoth();assert(Total(f.e)==40000);assert(Money(f.e,"host")==9900);
        assert(f.e.Join("host",f.id,f.now)==Error::Busy&&Money(f.e,"host")==9900);
        assert(f.e.Join("other",f.id,f.e.Find(f.id)->deadline)==Error::NotOpen);
        assert(f.e.Join("fourth",f.id,f.now)==Error::Invitation);
        assert(f.e.Leave("host",f.id,f.now)==Error::None&&Money(f.e,"host")==10000);
        assert(f.e.Leave("host",f.id,f.now)==Error::Missing&&Money(f.e,"host")==10000);
        assert(f.e.Join("host",f.id,f.now)==Error::None);assert(Total(f.e)==40000);
    }
    {
        Fixture f;auto p=FindRoute(1)->checkpoints.front();auto wrong=Sample(p,100);wrong.model=411;f.e.Observe("host",wrong,f.now);
        assert(f.e.Join("host",f.id,f.now)==Error::Vehicle&&Money(f.e,"host")==10000);
        f.e.Observe("host",Sample({0,0,0},100),++f.now);assert(f.e.Join("host",f.id,f.now)==Error::Location);
        uint64_t id=0;assert(f.e.Create("guest",5,100,2,{"other"},f.now,id)==Error::Locked);
    }
    {
        Fixture f;f.JoinBoth();f.e.Disconnect("guest",++f.now);assert(Money(f.e,"guest")==10000);
        f.now=f.e.Find(f.id)->deadline;f.e.Tick(f.now);assert(f.e.Find(f.id)->reason==Reason::InsufficientPlayers);
        assert(Money(f.e,"host")==10000&&Total(f.e)==40000);f.e.Disconnect("guest",++f.now);assert(Total(f.e)==40000);
    }
    {
        Fixture f;f.Start();f.e.World(false,0,false,++f.now);
        assert(f.e.Find(f.id)->phase==Phase::Settled&&Total(f.e)==40000&&Money(f.e,"guest")==10000);
        f.e.World(false,0,false,++f.now);assert(Total(f.e)==40000);
    }
    {
        Fixture f;f.Start();Finish(f);
        assert(Money(f.e,"host")==10100&&Money(f.e,"guest")==9900&&Total(f.e)==40000);
        assert(f.e.statistics.at("host").wins==1&&f.e.statistics.at("guest").losses==1);
        assert(f.e.statistics.at("host").reputation==0&&f.e.statistics.at("host").bestTimes.at(1)>0);
        auto transactions=f.e.cash.nextTransaction;for(unsigned i=0;i<10;++i)f.e.Tick(++f.now);
        assert(f.e.cash.nextTransaction==transactions&&Total(f.e)==40000);
        auto recovered=Restore(f.e);recovered.Recover(1);recovered.Recover(2);assert(Total(recovered)==40000&&Money(recovered,"host")==10100);
    }
    {
        Fixture f;f.Start();f.e.events.at(f.id).privateRace=false;Finish(f,40,40);
        assert(Money(f.e,"host")==10000&&Money(f.e,"guest")==10000);
        assert(f.e.events.at(f.id).participants[0].place==1&&f.e.events.at(f.id).participants[1].place==1);
        assert(f.e.statistics.at("host").reputation==10&&f.e.statistics.at("guest").reputation==10);
    }
    {
        Fixture f;f.Start();auto far=FindRoute(1)->checkpoints.back();
        for(unsigned i=0;i<3;++i)f.e.Observe("host",Sample(far,100),f.now+100*(i+1));
        const auto& p=f.e.Find(f.id)->participants.front();assert(p.outcome==Outcome::Forfeit&&p.nextCheckpoint==1);
        assert(Money(f.e,"host")==9900);  // teleport does not finish or refund a stake
        f.e.World(false,0,false,f.now+400);assert(Total(f.e)==40000);
    }
    {
        Fixture f;f.Start();auto changed=Sample(GridSpot(*FindRoute(1),0),999);f.e.Observe("host",changed,++f.now);
        assert(f.e.Find(f.id)->participants[0].outcome==Outcome::Forfeit);
        f.e.Disconnect("guest",++f.now);f.e.Tick(++f.now);f.e.Tick(++f.now);
        assert(f.e.Find(f.id)->reason==Reason::NoFinishers&&Total(f.e)==39800);
    }
    {
        Fixture f;f.Start();auto restored=Restore(f.e);assert(Total(restored)==40000);
        restored.Recover(1);assert(Money(restored,"host")==10000&&Money(restored,"guest")==10000);
        auto again=Restore(restored);again.Recover(2);assert(Total(again)==40000);
    }
    {
        Fixture f;f.JoinBoth();uint64_t second=0;assert(f.e.Create("other",2,200,2,{"fourth"},f.now,second)==Error::None);f.e.Tick(++f.now);
        auto start=FindRoute(2)->checkpoints.front();f.e.Observe("other",Sample(start,300),f.now);f.e.Observe("fourth",Sample(start,400),f.now);
        assert(f.e.Join("other",second,f.now)==Error::None&&f.e.Join("fourth",second,f.now)==Error::None);
        assert(f.e.events.at(f.id).participants.size()==2&&f.e.events.at(second).participants.size()==2&&Total(f.e)==40000);
        assert(f.e.Join("host",second,f.now)==Error::Invitation);
        f.e.World(false,0,false,++f.now);assert(Total(f.e)==40000);
    }
    {
        RaceEngine e(99);uint64_t now=100000;e.World(true,2,true,now);
        for(unsigned i=0;i<8;++i)e.cash.Open("player"+std::to_string(i),10000,true);
        for(unsigned race=0;race<4;++race){std::string a="player"+std::to_string(race*2),b="player"+std::to_string(race*2+1);uint64_t id=0;
            assert(e.Create(a,(uint16_t)(race+1),100,2,{b},now,id)==Error::None);e.Tick(now);
            auto p=FindRoute((uint16_t)(race+1))->checkpoints.front();e.Observe(a,Sample(p,(uint16_t)(race*2+1)),now);e.Observe(b,Sample(p,(uint16_t)(race*2+2)),now);
            assert(e.Join(a,id,now)==Error::None&&e.Join(b,id,now)==Error::None);
        }
        assert(e.events.size()==4&&Total(e)==80000);e.World(false,0,false,now+1);assert(Total(e)==80000);
    }
    {
        RaceEngine e(123);e.settings.minimumInterval=90000;e.settings.maximumInterval=240000;
        e.World(false,0,true,100000);e.Tick(999999);assert(e.events.empty());
        std::set<uint64_t> delays;
        for(unsigned i=0;i<30;++i){uint64_t now=1000000+i*1000000;e.World(true,0,true,now);now=e.nextGeneration;e.Tick(now);
            delays.insert(e.nextGeneration-now);for(const auto& [id,race]:e.events)if(race.phase<Phase::Settled)assert(FindRoute(race.route)->cityLevel==0);
            e.World(false,0,false,now+1);e.Tick(now+20000);e.Tick(now+30000);}
        assert(delays.size()>1);
        e.settings.maximumPlayers=1;e.World(true,4,true,99999999);e.Tick(100000000); // solo server cannot form a race
    }
    {
        economy::CashLedger cash;assert(cash.Open("guest",100,true));
        assert(cash.Sequence("guest",1)==economy::CashError::None);assert(cash.Sequence("guest",1)==economy::CashError::Sequence);
        assert(cash.ProposeIncome("guest",500)==economy::CashError::None&&cash.Find("guest")->cash==100);
        auto id=cash.credits.front().id;assert(cash.Decide(id,true)==economy::CashError::None&&cash.Find("guest")->cash==600);
        assert(cash.Decide(id,true)==economy::CashError::Missing&&cash.Find("guest")->cash==600);
        assert(cash.Change("guest",-601,economy::TransactionKind::Spending)==economy::CashError::Funds);
        assert(cash.Change("guest",economy::kMaximumCash,economy::TransactionKind::Income)==economy::CashError::Range);
        assert(cash.Find("guest")->cash==600);
    }
    {
        Fixture f;f.JoinBoth();auto candidate=f.e;candidate.cash.Change("host",economy::kMaximumCash-Money(candidate,"host"),economy::TransactionKind::Administrative);
        assert(!candidate.ValidCashHeadroom());assert(f.e.ValidCashHeadroom());
        Writer data;f.e.Write(data);for(size_t n=0;n<data.Size();++n){RaceEngine rejected;Reader r(data.Data(),n);assert(!rejected.Read(r));}
        data.U8(0);RaceEngine rejected;Reader r(data.Data(),data.Size());assert(!rejected.Read(r));
    }
    {
        Fixture f;f.JoinBoth();assert(f.e.cash.ProposeIncome("guest",100)==economy::CashError::None);
        Writer data;WriteSnapshot(data,f.e,"host",true,f.now,1);Snapshot host;Reader r(data.Data()+1,data.Size()-1);assert(host.Read(r)&&host.credits.size()==1);
        Writer other;WriteSnapshot(other,f.e,"fourth",false,f.now,1);Snapshot spectator;Reader s(other.Data()+1,other.Size()-1);
        assert(spectator.Read(s)&&spectator.events.empty()&&spectator.credits.empty());
        for(size_t n=0;n<data.Size()-1;++n){Snapshot rejected;Reader cut(data.Data()+1,n);assert(!rejected.Read(cut));}
    }
    std::puts("Street racing: routes, escrow, checkpoints, ties, concurrency, cash authority and recovery passed.");
}
