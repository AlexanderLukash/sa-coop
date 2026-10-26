#pragma once
#include "common/racing/RaceEngine.h"

namespace coop::street {
// Both directions are server service messages, never generic client relays.
enum class ClientOp : uint8_t { Sync, World, Observe, Join, Leave, Create, Cancel, CashChange, SetCash, DecideCredit, Initialize };
enum class ServerOp : uint8_t { Snapshot, Notice, CashSet };
struct CreditView { uint64_t id=0;std::string account;int32_t amount=0; };
struct ParticipantView {
    std::string account;
    uint8_t grid=0,place=0;
    Outcome outcome=Outcome::Registered;
    uint16_t next=1;
    uint32_t finish=0,paid=0;
};
struct EventView {
    uint64_t id=0;
    uint16_t route=0;
    Phase phase=Phase::Scheduled;
    Reason reason=Reason::None;
    uint32_t fee=0,remaining=0,elapsed=0;
    uint8_t capacity=0;
    bool privateRace=false;
    std::string organizer;
    std::vector<ParticipantView> participants;
};
struct Snapshot {
    uint64_t revision=0,sequence=0;
    int32_t cash=0;
    bool initialized=false,unlocked=false,available=false;
    uint8_t city=0;
    std::string account;
    Statistics statistics;
    std::vector<CreditView> credits;
    std::vector<EventView> events;
    bool Read(Reader& r) {
        revision=r.U64();sequence=r.U64();cash=r.I32();initialized=r.Bool();unlocked=r.Bool();available=r.Bool();city=r.U8();account=r.Str(64);
        statistics.races=r.U32();statistics.wins=r.U32();statistics.losses=r.U32();statistics.podiums=r.U32();statistics.reputation=r.U32();
        statistics.earned=r.U64();statistics.lost=r.U64();
        if(cash<0||cash>economy::kMaximumCash||city>4||account.empty())return false;
        credits.clear();events.clear();unsigned n=r.U8();if(n>64)return false;
        for(unsigned i=0;i<n&&r.Ok();++i){CreditView c;c.id=r.U64();c.account=r.Str(64);c.amount=r.I32();
            if(!c.id||c.account.empty()||c.amount<=0||c.amount>10000000)return false;credits.push_back(c);}
        n=r.U8();if(n>32)return false;
        for(unsigned i=0;i<n&&r.Ok();++i){EventView e;e.id=r.U64();e.route=r.U16();e.phase=(Phase)r.U8();e.reason=(Reason)r.U8();e.fee=r.U32();
            e.remaining=r.U32();e.elapsed=r.U32();e.capacity=r.U8();e.privateRace=r.Bool();e.organizer=r.Str(64);
            const Route* route=FindRoute(e.route);
            if(!e.id||!route||e.phase>Phase::Closed||e.reason>Reason::Restart||e.fee>1000000||e.capacity<2||e.capacity>8||e.remaining>900000)return false;
            unsigned count=r.U8();if(count>e.capacity)return false;std::set<std::string> unique;
            for(unsigned k=0;k<count&&r.Ok();++k){ParticipantView p;p.account=r.Str(64);p.grid=r.U8();p.place=r.U8();p.outcome=(Outcome)r.U8();p.next=r.U16();p.finish=r.U32();p.paid=r.U32();
                if(p.account.empty()||!unique.insert(p.account).second||p.grid>=8||p.place>8||p.outcome>Outcome::Refunded||p.next<1||p.next>route->checkpoints.size()||p.paid>8000000)return false;
                e.participants.push_back(p);}
            events.push_back(e);
        }
        return r.Ok()&&!r.Remaining();
    }
};
inline void WriteSnapshot(Writer& w,const RaceEngine& engine,const std::string& key,bool host,uint64_t now,uint64_t revision) {
    const auto* account=engine.cash.Find(key);
    w.U8((uint8_t)ServerOp::Snapshot);w.U64(revision);w.U64(account?account->sequence:0);w.I32(account?(int32_t)account->cash:0);
    w.Bool(account&&account->initialized);w.Bool(engine.unlocked);w.Bool(engine.worldAvailable);w.U8(engine.city);w.Str(key,64);
    auto it=engine.statistics.find(key);Statistics stats=it==engine.statistics.end()?Statistics{}:it->second;
    w.U32(stats.races);w.U32(stats.wins);w.U32(stats.losses);w.U32(stats.podiums);w.U32(stats.reputation);w.U64(stats.earned);w.U64(stats.lost);
    w.U8(host?(uint8_t)engine.cash.credits.size():0);
    if(host)for(const auto& c:engine.cash.credits){w.U64(c.id);w.Str(c.account,64);w.I32(c.amount);}
    std::vector<const Event*> visible;
    for(const auto& [id,e]:engine.events)if(engine.Visible(e,key))visible.push_back(&e);
    w.U8((uint8_t)visible.size());
    for(const auto* event:visible){const auto& e=*event;w.U64(e.id);w.U16(e.route);w.U8((uint8_t)e.phase);w.U8((uint8_t)e.reason);w.U32(e.fee);
        w.U32((uint32_t)(e.deadline>now?std::min<uint64_t>(e.deadline-now,900000):0));w.U32(e.started&&now>=e.started?(uint32_t)std::min<uint64_t>(now-e.started,900000):0);
        w.U8(e.capacity);w.Bool(e.privateRace);w.Str(e.organizer,64);w.U8((uint8_t)e.participants.size());
        for(const auto& p:e.participants){w.Str(p.account,64);w.U8(p.grid);w.U8(p.place);w.U8((uint8_t)p.outcome);w.U16(p.nextCheckpoint);w.U32(p.finishMs);w.U32((uint32_t)p.paid);}
    }
}
}
