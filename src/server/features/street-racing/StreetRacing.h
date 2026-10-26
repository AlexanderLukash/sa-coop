#pragma once
#include <functional>
#include <string>
#include <vector>
#include "common/racing/RaceProtocol.h"

namespace coop::server {
struct StreetPeer { uint8_t id=0;std::string account;bool host=false,playing=false; };
class StreetRacing {
public:
    using Send=std::function<void(uint8_t,const Writer&)>;
    ~StreetRacing();
    bool Open(const std::wstring& directory,uint8_t maximumPlayers,const street::Settings& settings,Send send);
    bool Connect(const StreetPeer& peer,int32_t legacyCash,bool hasLegacy);
    void Disconnect(const StreetPeer& peer,uint64_t now);
    void Handle(const StreetPeer& peer,Reader& reader,const std::vector<StreetPeer>& peers,uint64_t now);
    void Tick(const std::vector<StreetPeer>& peers,uint64_t now);
    void WorldReset(uint64_t now);
    bool MissionReward(const StreetPeer& host,uint64_t sequence,int32_t amount,const std::vector<StreetPeer>& peers);
private:
    street::RaceEngine m_engine;
    Send m_send;
    std::wstring m_path;
    void* m_writerLock=nullptr;
    uint64_t m_revision=1,m_sentRevision=0,m_lastSend=0,m_lastTick=0,m_lastWorld=0;
    uint32_t m_observationSequence=0;
    struct Rate {uint64_t at=0,rejected=0;unsigned tokens=64;};
    std::map<std::string,Rate> m_rates;
    bool Commit(street::RaceEngine&& engine,bool force=false);
    void Snapshot(const StreetPeer& peer,uint64_t now);
    void Notice(const StreetPeer& peer,street::Error error);
};
}
