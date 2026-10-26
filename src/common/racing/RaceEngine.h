#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "common/economy/CashLedger.h"
#include "common/racing/Routes.h"

namespace coop::street {
enum class Phase : uint8_t { Scheduled, Registration, Countdown, Racing, Finished, Settled, Closed };
enum class Outcome : uint8_t { Registered, Racing, Finished, Forfeit, Refunded };
enum class Reason : uint8_t { None, Completed, InsufficientPlayers, Technical, NoFinishers, WorldChanged, HostLeft, Restart };
enum class Error : uint8_t { None, Locked, Missing, NotOpen, Full, Funds, Vehicle, Location, Busy, Invitation, Limit, Replay, Storage };
struct Settings {
    uint32_t minimumInterval=90000, maximumInterval=240000, registrationMs=90000, countdownMs=8000;
    uint32_t maximumRaceMs=600000, afterFirstFinishMs=60000;
    uint8_t maximumPublicEvents=2, maximumEvents=8, maximumPlayers=8;
};
struct Observation {
    Vec3 position;
    uint16_t vehicle=0, model=0;
    bool driver=false, alive=true, available=true;
    uint8_t area=0;
    uint64_t received=0;
};
struct Participant {
    std::string account;
    uint16_t vehicle=0, model=0, nextCheckpoint=1;
    uint8_t grid=0, place=0, invalidSamples=0;
    Outcome outcome=Outcome::Registered;
    uint32_t finishMs=0, stake=0;
    uint64_t paid=0, lastGateMs=0;
    Vec3 acceptedPosition;
    uint64_t acceptedAt=0;
};
struct Event {
    uint64_t id=0;
    uint16_t route=0;
    Phase phase=Phase::Scheduled;
    Reason reason=Reason::None;
    std::string organizer;
    std::set<std::string> invited;
    bool privateRace=false;
    uint8_t capacity=8;
    uint32_t fee=0;
    uint64_t deadline=0, started=0, firstFinish=0;
    std::vector<Participant> participants;
};
struct Statistics {
    uint32_t races=0,wins=0,losses=0,podiums=0,reputation=0,streak=0;
    uint64_t earned=0,lost=0;
    std::map<uint16_t,uint32_t> bestTimes;
};

// Pure server state. Engine time is supplied by the server, never by racers.
// Cash and escrow share one durable document so a crash cannot commit half a bet.
class RaceEngine {
public:
    Settings settings;
    economy::CashLedger cash;
    std::map<std::string,Statistics> statistics;
    std::map<uint64_t,Event> events;
    std::map<std::string,Observation> observed;
    uint64_t revision=0,durableRevision=0,nextEvent=1,nextGeneration=0,randomState=1;
    bool unlocked=false,worldAvailable=false;
    uint8_t city=0;

    explicit RaceEngine(uint64_t seed=1):randomState(seed?seed:1){}
    const Event* Find(uint64_t id) const;
    const Event* Participation(const std::string& account) const;
    bool Visible(const Event& event,const std::string& account) const;
    bool ValidCashHeadroom() const;
    void World(bool cesarPassed,uint8_t cityLevel,bool available,uint64_t now);
    void Observe(const std::string& account,const Observation& sample,uint64_t now);
    void Disconnect(const std::string& account,uint64_t now);
    Error Create(const std::string& owner,uint16_t route,uint32_t fee,uint8_t capacity,
                 const std::set<std::string>& invited,uint64_t now,uint64_t& id);
    Error Join(const std::string& account,uint64_t event,uint64_t now);
    Error Leave(const std::string& account,uint64_t event,uint64_t now);
    Error Cancel(const std::string& account,uint64_t event,uint64_t now);
    void Tick(uint64_t now);
    void Recover(uint64_t now);
    void Write(Writer& writer) const;
    bool Read(Reader& reader);
private:
    uint32_t Random(uint32_t low,uint32_t high);
    bool RouteInUse(uint16_t route) const;
    void Changed(bool durable=false);
    void CancelEvent(Event& event,Reason reason,uint64_t now);
    void Settle(Event& event,uint64_t now);
    void Progress(Event& event,Participant& racer,const Observation& before,const Observation& after,uint64_t now);
};
}
