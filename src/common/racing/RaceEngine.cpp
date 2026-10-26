#include "common/racing/RaceEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace coop::street {
using economy::CashError;
using economy::TransactionKind;
namespace {
bool Finite(Vec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
    std::abs(p.x)<4000&&std::abs(p.y)<4000&&p.z>-200&&p.z<500;}
bool Active(Phase p) {return p<Phase::Settled;}
}
const Event* RaceEngine::Find(uint64_t id) const {auto it=events.find(id);return it==events.end()?nullptr:&it->second;}
const Event* RaceEngine::Participation(const std::string& key) const {
    for(const auto& [id,e]:events)if(e.phase<Phase::Settled)
        for(const auto& p:e.participants)if(p.account==key)return &e;
    return nullptr;
}
bool RaceEngine::Visible(const Event& e,const std::string& key) const {
    return !e.privateRace||e.organizer==key||e.invited.count(key)||
        std::any_of(e.participants.begin(),e.participants.end(),[&](const auto& p){return p.account==key;});
}
bool RaceEngine::ValidCashHeadroom() const {
    std::map<std::string,int64_t> reserved;
    for(const auto& [id,e]:events)for(const auto& p:e.participants)reserved[p.account]+=p.stake;
    for(const auto& [key,a]:cash.accounts)if(a.cash+reserved[key]>economy::kMaximumCash)return false;
    return true;
}
void RaceEngine::Changed(bool durable) {++revision;if(durable)++durableRevision;}
uint32_t RaceEngine::Random(uint32_t low,uint32_t high) {
    if(high<low)return low;
    randomState^=randomState<<13;randomState^=randomState>>7;randomState^=randomState<<17;
    return low+(uint32_t)(randomState%(uint64_t(high)-low+1));
}
bool RaceEngine::RouteInUse(uint16_t route) const {
    for(const auto& [id,e]:events)if(e.route==route&&e.phase<Phase::Settled)return true;
    return false;
}
void RaceEngine::World(bool passed,uint8_t level,bool available,uint64_t now) {
    if(level>4)return;
    bool change=unlocked!=passed||city!=level||worldAvailable!=available;
    unlocked=passed;city=level;worldAvailable=available;
    if(change){
        for(auto& [id,e]:events)if(Active(e.phase)&&(!available||!Available(*FindRoute(e.route),passed,level)))
            CancelEvent(e,Reason::WorldChanged,now);
        nextGeneration=now+Random(10000,30000);Changed();
    }
}
Error RaceEngine::Create(const std::string& owner,uint16_t routeId,uint32_t fee,uint8_t capacity,
                         const std::set<std::string>& invited,uint64_t now,uint64_t& id) {
    const Route* route=FindRoute(routeId);
    if(!route)return Error::Missing;
    if(!worldAvailable||!Available(*route,unlocked,city))return Error::Locked;
    if(capacity<route->minimum||capacity>std::min(route->maximum,settings.maximumPlayers)||fee>route->maximumStake)return Error::Limit;
    if(invited.empty()||(invited.size()==1&&invited.count(owner)))return Error::Invitation;
    unsigned active=0;
    for(const auto& [key,e]:events){if(e.phase<Phase::Settled){++active;if(e.privateRace&&e.organizer==owner)return Error::Busy;}}
    if(events.size()>=32||active>=settings.maximumEvents||RouteInUse(routeId))return Error::Busy;
    Event event;event.id=nextEvent++;event.route=routeId;event.privateRace=true;event.organizer=owner;event.invited=invited;
    event.invited.insert(owner);event.fee=fee;event.capacity=capacity;event.deadline=now+settings.registrationMs;
    id=event.id;events.emplace(id,std::move(event));Changed(true);return Error::None;
}
Error RaceEngine::Join(const std::string& key,uint64_t id,uint64_t now) {
    auto it=events.find(id);if(it==events.end())return Error::Missing;
    Event& e=it->second;const Route& route=*FindRoute(e.route);
    if(!worldAvailable||!Available(route,unlocked,city))return Error::Locked;
    if(e.phase!=Phase::Registration||now>=e.deadline)return Error::NotOpen;
    if(!Visible(e,key))return Error::Invitation;
    if(Participation(key))return Error::Busy;
    if(e.participants.size()>=e.capacity)return Error::Full;
    auto sample=observed.find(key);
    if(sample==observed.end()||now<sample->second.received||now-sample->second.received>2000)return Error::Location;
    const Observation& s=sample->second;
    if(!s.driver||!s.alive||!s.available||s.area||!s.vehicle||!Eligible(route,s.model))return Error::Vehicle;
    if(Distance(s.position,route.checkpoints.front())>60)return Error::Location;
    if(cash.Change(key,-(int64_t)e.fee,TransactionKind::Stake,e.id)!=CashError::None)return Error::Funds;
    Participant p;p.account=key;p.model=s.model;p.vehicle=s.vehicle;p.stake=e.fee;
    e.participants.push_back(p);Changed(true);return Error::None;
}
Error RaceEngine::Leave(const std::string& key,uint64_t id,uint64_t now) {
    auto event=events.find(id);if(event==events.end())return Error::Missing;Event& e=event->second;
    auto it=std::find_if(e.participants.begin(),e.participants.end(),[&](const auto& p){return p.account==key;});
    if(it==e.participants.end())return Error::Missing;
    if(e.phase==Phase::Registration&&now<e.deadline){
        if(cash.Change(key,it->stake,TransactionKind::Refund,id)!=CashError::None)return Error::Funds;
        e.participants.erase(it);Changed(true);return Error::None;
    }
    if(e.phase==Phase::Countdown){CancelEvent(e,Reason::Technical,now);return Error::None;}
    if(e.phase!=Phase::Racing||it->outcome!=Outcome::Racing)return Error::NotOpen;
    it->outcome=Outcome::Forfeit;Changed();return Error::None;
}
Error RaceEngine::Cancel(const std::string& key,uint64_t id,uint64_t now) {
    auto it=events.find(id);if(it==events.end())return Error::Missing;
    if(!it->second.privateRace||it->second.organizer!=key||it->second.phase>Phase::Registration)return Error::NotOpen;
    CancelEvent(it->second,Reason::Technical,now);return Error::None;
}
void RaceEngine::CancelEvent(Event& e,Reason reason,uint64_t now) {
    if(e.phase>=Phase::Settled)return;
    for(const auto& p:e.participants){auto* a=cash.Find(p.account);if(!a||a->cash+p.stake>economy::kMaximumCash)return;}
    for(auto& p:e.participants){
        // Refunds cannot overflow: while a stake is reserved, incoming credits
        // must leave enough capacity for all refundable escrow (checked by server).
        if(p.stake&&cash.Change(p.account,p.stake,TransactionKind::Refund,e.id)!=CashError::None)return;
        p.paid=p.stake;p.stake=0;p.outcome=Outcome::Refunded;
    }
    e.phase=Phase::Settled;e.reason=reason;e.deadline=now+15000;Changed(true);
}
void RaceEngine::Disconnect(const std::string& key,uint64_t now) {
    observed.erase(key);
    for(auto& [id,e]:events){
        if(e.phase>Phase::Racing)continue;
        if(e.phase<=Phase::Registration){
            if(e.privateRace&&e.organizer==key){CancelEvent(e,Reason::Technical,now);continue;}
            auto p=std::find_if(e.participants.begin(),e.participants.end(),[&](const auto& p){return p.account==key;});
            if(p!=e.participants.end()){
                if(cash.Change(key,p->stake,TransactionKind::Refund,id)==CashError::None){e.participants.erase(p);Changed(true);}
            }
        }else if(std::any_of(e.participants.begin(),e.participants.end(),[&](const auto& p){return p.account==key&&p.outcome!=Outcome::Finished;})){
            if(e.phase==Phase::Countdown)CancelEvent(e,Reason::Technical,now);
            else for(auto& p:e.participants)if(p.account==key&&p.outcome==Outcome::Racing){p.outcome=Outcome::Forfeit;Changed();}
        }
    }
}
void RaceEngine::Observe(const std::string& key,const Observation& sample,uint64_t now) {
    if(!cash.Find(key)||!Finite(sample.position))return;
    auto it=observed.find(key);Observation after=sample;after.received=now;
    if(it!=observed.end()&&now<=it->second.received)return;
    Observation before=it==observed.end()?after:it->second;
    observed[key]=after;
    for(auto& [id,e]:events)if(e.phase==Phase::Racing)
        for(auto& p:e.participants)if(p.account==key&&p.outcome==Outcome::Racing)Progress(e,p,before,after,now);
}
void RaceEngine::Progress(Event& e,Participant& p,const Observation& before,const Observation& s,uint64_t now) {
    const Route& route=*FindRoute(e.route);
    if(!s.driver||!s.alive||!s.available||s.area||s.vehicle!=p.vehicle||s.model!=p.model||!Eligible(route,s.model)){
        p.outcome=Outcome::Forfeit;Changed();return;
    }
    (void)before;
    uint64_t age=now-p.acceptedAt;
    float maximum=MaximumSpeed(ClassOf(s.model));
    if(!age||age>3000||Distance(p.acceptedPosition,s.position)>20+maximum*(float(age)/1000.0f)){
        if(++p.invalidSamples>=3){p.outcome=Outcome::Forfeit;Changed();}return;
    }
    p.invalidSamples=0;
    Vec3 previous=p.acceptedPosition;p.acceptedPosition=s.position;p.acceptedAt=now;
    if(p.nextCheckpoint>=route.checkpoints.size())return;
    Vec3 gate=route.checkpoints[p.nextCheckpoint];
    float leg=Distance(route.checkpoints[p.nextCheckpoint-1],gate);
    if(now<p.lastGateMs||float(now-p.lastGateMs)<std::max(0.0f,leg-24)*1000.0f/maximum)return;
    if(DistanceToSegment(gate,previous,s.position)>12)return;
    if(p.nextCheckpoint+1==route.checkpoints.size()&&
       now-e.started<uint64_t(std::max(0.0f,route.Length()-24*route.checkpoints.size())*1000/maximum))return;
    ++p.nextCheckpoint;p.lastGateMs=now;Changed();
    if(p.nextCheckpoint==route.checkpoints.size()){
        p.outcome=Outcome::Finished;p.finishMs=(uint32_t)(now-e.started);
        if(!e.firstFinish)e.firstFinish=now;
    }
}
void RaceEngine::Settle(Event& e,uint64_t now) {
    if(e.phase!=Phase::Finished)return;
    std::vector<Participant*> finishers;
    uint64_t pool=0;
    for(auto& p:e.participants){pool+=p.stake;if(p.outcome==Outcome::Finished)finishers.push_back(&p);}
    std::sort(finishers.begin(),finishers.end(),[](auto a,auto b){return a->finishMs!=b->finishMs?a->finishMs<b->finishMs:a->account<b->account;});
    unsigned winners=0;
    if(!finishers.empty())while(winners<finishers.size()&&finishers[winners]->finishMs-finishers[0]->finishMs<=250)++winners;
    for(unsigned i=0;i<finishers.size();++i)finishers[i]->place=i<winners?1:uint8_t(i+1);
    // Verify the entire payout before committing any winner. Insufficient cash
    // headroom cancels/refunds the event rather than losing or minting money.
    for(unsigned i=0;i<winners;++i){
        uint64_t prize=pool/winners+(i<pool%winners?1:0);
        auto* a=cash.Find(finishers[i]->account);
        if(!a||a->cash+(int64_t)prize>economy::kMaximumCash){CancelEvent(e,Reason::Technical,now);return;}
    }
    for(unsigned i=0;i<winners;++i){
        uint64_t prize=pool/winners+(i<pool%winners?1:0);
        cash.Change(finishers[i]->account,(int64_t)prize,TransactionKind::Prize,e.id);finishers[i]->paid=prize;
    }
    for(auto& p:e.participants){
        Statistics& s=statistics[p.account];++s.races;
        if(p.place==1){++s.wins;++s.streak;}else{++s.losses;s.streak=0;}
        if(p.place&&p.place<=3)++s.podiums;
        if(p.paid>=p.stake)s.earned+=p.paid-p.stake;else s.lost+=p.stake-p.paid;
        if(p.outcome==Outcome::Finished){auto& best=s.bestTimes[e.route];if(!best||p.finishMs<best)best=p.finishMs;}
        if(!e.privateRace&&e.fee&&finishers.size()>=2&&p.outcome==Outcome::Finished)s.reputation+=p.place==1?10:2;
        p.stake=0;
    }
    e.phase=Phase::Settled;e.reason=finishers.empty()?Reason::NoFinishers:Reason::Completed;e.deadline=now+15000;Changed(true);
}
void RaceEngine::Tick(uint64_t now) {
    for(auto& [id,e]:events){
        if(e.phase==Phase::Scheduled){e.phase=Phase::Registration;Changed();}
        else if(e.phase==Phase::Registration&&now>=e.deadline){
            const Route& route=*FindRoute(e.route);
            if(e.participants.size()<route.minimum){CancelEvent(e,Reason::InsufficientPlayers,now);continue;}
            bool valid=true;
            for(unsigned i=0;i<e.participants.size();++i){
                auto& p=e.participants[i];p.grid=(uint8_t)i;auto it=observed.find(p.account);
                if(it==observed.end()||now<it->second.received||now-it->second.received>2000||!it->second.driver||
                   !it->second.alive||!it->second.available||it->second.area||it->second.vehicle!=p.vehicle||
                   it->second.model!=p.model||!Eligible(route,p.model)||Distance(it->second.position,route.checkpoints.front())>80)valid=false;
            }
            if(!valid){CancelEvent(e,Reason::Technical,now);continue;}
            e.phase=Phase::Countdown;e.deadline=now+settings.countdownMs;Changed(true);
        }else if(e.phase==Phase::Countdown&&now>=e.deadline){
            bool valid=true;const Route& route=*FindRoute(e.route);
            for(auto& p:e.participants){auto it=observed.find(p.account);
                if(it==observed.end()||now<it->second.received||now-it->second.received>2000||!it->second.driver||
                   !it->second.alive||!it->second.available||it->second.area||it->second.model!=p.model||it->second.vehicle!=p.vehicle||
                   Distance(it->second.position,GridSpot(route,p.grid))>10)valid=false;
            }
            if(!valid){CancelEvent(e,Reason::Technical,now);continue;}
            e.phase=Phase::Racing;e.started=now;e.deadline=now+settings.maximumRaceMs;
            for(auto& p:e.participants){p.outcome=Outcome::Racing;p.lastGateMs=now;p.acceptedAt=now;p.acceptedPosition=observed.at(p.account).position;}
            Changed(true);
        }else if(e.phase==Phase::Racing){
            bool ongoing=false;
            for(auto& p:e.participants)if(p.outcome==Outcome::Racing){
                auto it=observed.find(p.account);
                if(it==observed.end()||now<it->second.received||now-it->second.received>10000||now>=e.deadline||
                   (e.firstFinish&&now-e.firstFinish>=settings.afterFirstFinishMs)){p.outcome=Outcome::Forfeit;Changed();}
                else ongoing=true;
            }
            if(!ongoing){e.phase=Phase::Finished;Changed();}
        }else if(e.phase==Phase::Finished)Settle(e,now);
        else if(e.phase==Phase::Settled&&now>=e.deadline){e.phase=Phase::Closed;e.deadline=now+5000;Changed();}
    }
    for(auto it=events.begin();it!=events.end();) {
        if(it->second.phase==Phase::Closed&&now>=it->second.deadline){it=events.erase(it);Changed(true);}else ++it;
    }
    if(settings.maximumPlayers<2||!worldAvailable||!unlocked||now<nextGeneration)return;
    unsigned publicCount=0,activeCount=0;
    for(const auto& [id,e]:events)if(e.phase<Phase::Settled){++activeCount;if(!e.privateRace)++publicCount;}
    if(events.size()<32&&publicCount<settings.maximumPublicEvents&&activeCount<settings.maximumEvents){
        std::vector<const Route*> eligible;
        for(const auto& r:Routes())if(Available(r,unlocked,city)&&!RouteInUse(r.id))eligible.push_back(&r);
        if(!eligible.empty()){
            const Route& r=*eligible[Random(0,(uint32_t)eligible.size()-1)];Event e;e.id=nextEvent++;e.route=r.id;
            e.capacity=(uint8_t)Random(r.minimum,std::min(r.maximum,settings.maximumPlayers));
            e.fee=Random(r.minimumStake,r.maximumStake);e.deadline=now+settings.registrationMs;
            events.emplace(e.id,e);Changed(true);
        }
    }
    nextGeneration=now+Random(settings.minimumInterval,std::max(settings.minimumInterval,settings.maximumInterval));
}
void RaceEngine::Recover(uint64_t now) {
    observed.clear();worldAvailable=unlocked=false;nextGeneration=now+10000;
    for(auto& [id,e]:events)if(e.phase<Phase::Settled)CancelEvent(e,Reason::Restart,now);
    for(auto& [id,e]:events)if(e.phase>=Phase::Settled)e.deadline=now+15000;
}
void RaceEngine::Write(Writer& w) const {
    w.U32(0x31525453);w.U16(1);cash.Write(w);w.U64(nextEvent);w.U64(randomState);
    w.U16((uint16_t)statistics.size());
    for(const auto& [key,s]:statistics){w.Str(key,64);w.U32(s.races);w.U32(s.wins);w.U32(s.losses);w.U32(s.podiums);w.U32(s.reputation);w.U32(s.streak);
        w.U64(s.earned);w.U64(s.lost);w.U8((uint8_t)s.bestTimes.size());for(auto [route,time]:s.bestTimes){w.U16(route);w.U32(time);}}
    w.U16((uint16_t)events.size());
    for(const auto& [id,e]:events){w.U64(id);w.U16(e.route);w.U8((uint8_t)e.phase);w.U8((uint8_t)e.reason);w.Bool(e.privateRace);w.Str(e.organizer,64);
        w.U8(e.capacity);w.U32(e.fee);w.U64(e.deadline);w.U64(e.started);w.U64(e.firstFinish);w.U8((uint8_t)e.invited.size());for(const auto& key:e.invited)w.Str(key,64);
        w.U8((uint8_t)e.participants.size());for(const auto& p:e.participants){w.Str(p.account,64);w.U16(p.vehicle);w.U16(p.model);w.U16(p.nextCheckpoint);
            w.U8(p.grid);w.U8(p.place);w.U8((uint8_t)p.outcome);w.U32(p.finishMs);w.U32(p.stake);w.U64(p.paid);w.U64(p.lastGateMs);}}
}
bool RaceEngine::Read(Reader& r) {
    RaceEngine loaded;loaded.settings=settings;
    if(r.U32()!=0x31525453||r.U16()!=1||!loaded.cash.Read(r))return false;
    loaded.nextEvent=r.U64();loaded.randomState=r.U64();if(!loaded.nextEvent||!loaded.randomState)return false;
    uint16_t n=r.U16();if(n>4096)return false;
    for(unsigned i=0;i<n&&r.Ok();++i){auto key=r.Str(64);Statistics s;s.races=r.U32();s.wins=r.U32();s.losses=r.U32();s.podiums=r.U32();s.reputation=r.U32();s.streak=r.U32();
        s.earned=r.U64();s.lost=r.U64();uint8_t count=r.U8();if(!loaded.cash.Find(key)||count>Routes().size())return false;
        for(unsigned k=0;k<count;++k){auto route=r.U16();auto time=r.U32();if(!FindRoute(route)||!time||!s.bestTimes.emplace(route,time).second)return false;}
        if(!loaded.statistics.emplace(key,s).second)return false;
    }
    n=r.U16();if(n>64)return false;std::map<std::string,int64_t> reserved;
    for(unsigned i=0;i<n&&r.Ok();++i){Event e;e.id=r.U64();e.route=r.U16();e.phase=(Phase)r.U8();e.reason=(Reason)r.U8();e.privateRace=r.Bool();e.organizer=r.Str(64);
        e.capacity=r.U8();e.fee=r.U32();e.deadline=r.U64();e.started=r.U64();e.firstFinish=r.U64();
        if(!e.id||e.id>=loaded.nextEvent||!FindRoute(e.route)||e.phase>Phase::Closed||e.reason>Reason::Restart||e.capacity<2||e.capacity>8||e.fee>1000000)return false;
        uint8_t count=r.U8();if(count>8)return false;for(unsigned k=0;k<count;++k)if(!e.invited.insert(r.Str(64)).second)return false;
        count=r.U8();if(count>e.capacity)return false;std::set<std::string> unique;
        for(unsigned k=0;k<count;++k){Participant p;p.account=r.Str(64);p.vehicle=r.U16();p.model=r.U16();p.nextCheckpoint=r.U16();
            p.grid=r.U8();p.place=r.U8();p.outcome=(Outcome)r.U8();p.finishMs=r.U32();p.stake=r.U32();p.paid=r.U64();p.lastGateMs=r.U64();
            if(!loaded.cash.Find(p.account)||!unique.insert(p.account).second||p.outcome>Outcome::Refunded||p.stake>e.fee||p.paid>8000000||
               p.nextCheckpoint<1||p.nextCheckpoint>FindRoute(e.route)->checkpoints.size()||p.grid>=8||p.place>8||!Eligible(*FindRoute(e.route),p.model))return false;
            if(e.phase>=Phase::Settled&&p.stake)return false;
            reserved[p.account]+=p.stake;e.participants.push_back(p);
        }
        if(!loaded.events.emplace(e.id,e).second)return false;
    }
    for(auto [key,amount]:reserved)if(loaded.cash.Find(key)->cash+amount>economy::kMaximumCash)return false;
    if(!r.Ok()||r.Remaining())return false;*this=std::move(loaded);return true;
}
}
