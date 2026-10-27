#include "features/street-racing/StreetRacing.h"
#include <windows.h>
#include <algorithm>
#include <limits>
#include "common/api/protocol/Protocol.h"
#include "common/lib/log/Log.h"
#include "common/lib/sync/StateTiming.h"
#include "common/lib/util/Util.h"

namespace coop::server {
using namespace street;
namespace {
Error CashResult(economy::CashError e) {
    switch(e){case economy::CashError::None:return Error::None;case economy::CashError::Sequence:return Error::Replay;
        case economy::CashError::Funds:return Error::Funds;case economy::CashError::Missing:return Error::Missing;default:return Error::Limit;}
}
}
StreetRacing::~StreetRacing(){if(m_writerLock)CloseHandle((HANDLE)m_writerLock);}
bool StreetRacing::Open(const std::wstring& directory,uint8_t maximumPlayers,const Settings& settings,Send send) {
    m_path=directory+L"street-racing.bin";m_send=std::move(send);m_engine.settings=settings;m_engine.settings.maximumPlayers=maximumPlayers;
    // Keep the cash ledger and refund old escrow, but never schedule new races.
    m_engine.settings.maximumPublicEvents=0;
    HANDLE lock=CreateFileW((m_path+L".lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(lock==INVALID_HANDLE_VALUE)return false;
    m_writerLock=lock;
    m_engine.randomState=NowMs()^0x8AB71D39E35Bull;
    if(FileExists(m_path)){
        std::vector<uint8_t> data;if(!ReadFileBytes(m_path,data)||data.size()<4||data.size()>8*1024*1024)return false;
        Reader checksum(data.data()+data.size()-4,4);
        if(Crc32(data.data(),data.size()-4)!=checksum.U32())return false;
        Reader reader(data.data(),data.size()-4);if(!m_engine.Read(reader))return false;
        auto recovered=m_engine;recovered.Recover(NowMs());
        // Income queued by older versions belongs to its player; apply it once on migration.
        if(recovered.cash.CreditPendingIncome()!=economy::CashError::None)return false;
        if(!Commit(std::move(recovered),true))return false;
    }
    return true;
}
bool StreetRacing::Commit(RaceEngine&& candidate,bool force) {
    if(!candidate.ValidCashHeadroom())return false;
    bool durable=force||candidate.durableRevision!=m_engine.durableRevision||candidate.cash.revision!=m_engine.cash.revision;
    bool changed=durable||candidate.revision!=m_engine.revision;
    if(durable){
        Writer file;candidate.Write(file);file.U32(Crc32(file.Data(),file.Size()));
        if(!WriteFileBytes(m_path,file.Data(),file.Size())){LOGE("Street racing/economy transaction was not committed: storage failure");return false;}
    }
    m_engine=std::move(candidate);if(changed)++m_revision;return true;
}
bool StreetRacing::Connect(const StreetPeer& peer,int32_t legacyCash,bool hasLegacy) {
    auto candidate=m_engine;
    if(!candidate.cash.Open(peer.account,std::clamp<int64_t>(legacyCash,0,economy::kMaximumCash),hasLegacy))return false;
    if(!Commit(std::move(candidate)))return false;
    m_rates[peer.account]={NowMs(),0,64};Snapshot(peer,NowMs());return true;
}
void StreetRacing::Snapshot(const StreetPeer& peer,uint64_t now) {
    Writer w(proto::S_STREET_RACING);WriteSnapshot(w,m_engine,peer.account,peer.host,now,m_revision);m_send(peer.id,w);
}
void StreetRacing::Notice(const StreetPeer& peer,Error error) {
    if(error==Error::None)return;
    Writer w(proto::S_STREET_RACING);w.U8((uint8_t)ServerOp::Notice);w.U8((uint8_t)error);m_send(peer.id,w);
}
void StreetRacing::Disconnect(const StreetPeer& peer,uint64_t now) {
    m_rates.erase(peer.account);
    auto candidate=m_engine;candidate.Disconnect(peer.account,now);
    if(peer.host){candidate.World(false,0,false,now);m_lastWorld=0;m_observationSequence=0;}
    Commit(std::move(candidate));
}
void StreetRacing::WorldReset(uint64_t now) {auto candidate=m_engine;candidate.World(false,0,false,now);Commit(std::move(candidate));m_lastWorld=0;}
void StreetRacing::Handle(const StreetPeer& peer,Reader& r,const std::vector<StreetPeer>& peers,uint64_t now) {
    if(r.Remaining()>2048)return;
    auto& rate=m_rates[peer.account];if(!rate.at)rate.at=now;
    uint64_t refill=now>=rate.at?(now-rate.at)/25:0;
    if(refill){rate.tokens=(unsigned)std::min<uint64_t>(64,rate.tokens+refill);rate.at+=refill*25;}
    if(!rate.tokens){
        if(now-rate.rejected>1000){rate.rejected=now;Notice(peer,Error::Replay);Snapshot(peer,now);}return;
    }
    --rate.tokens;
    ClientOp op=(ClientOp)r.U8();
    if(op==ClientOp::Sync){if(r.Ok()&&!r.Remaining())Snapshot(peer,now);return;}
    auto candidate=m_engine;Error result=Error::None;std::string changedAccount;int32_t changedCash=0;uint64_t requestSequence=0;
    if(op==ClientOp::World){
        bool passed=r.Bool();uint8_t city=r.U8();bool available=r.Bool();
        if(!peer.host||!r.Ok()||r.Remaining()||city>4)return;
        candidate.World(false,city,false,now);
        (void)passed;(void)available;m_lastWorld=now;
    }else if(op==ClientOp::Observe){
        uint32_t frame=r.U32();unsigned count=r.U8();
        if(!peer.host||count>proto::kMaxPlayers||!sync::IsNewer(frame,m_observationSequence))return;
        std::vector<std::pair<std::string,Observation>> observations;std::set<uint8_t> seen;
        for(unsigned i=0;i<count&&r.Ok();++i){
            uint8_t id=r.U8();Observation s;s.position=r.Vec();s.vehicle=r.U16();s.model=r.U16();s.driver=r.Bool();s.alive=r.Bool();s.available=r.Bool();s.area=r.U8();
            auto p=std::find_if(peers.begin(),peers.end(),[&](const auto& p){return p.id==id&&p.playing;});
            if(!seen.insert(id).second||s.area>18)return;
            if(p!=peers.end())observations.emplace_back(p->account,s);
        }
        if(!r.Ok()||r.Remaining())return;
        for(const auto& [key,sample]:observations)candidate.Observe(key,sample,now);
        m_observationSequence=frame;
    }else{
        uint64_t sequence=r.U64();
        requestSequence=sequence;
        // Decode and validate the entire request before consuming its sequence.
        uint64_t event=0,credit=0;uint16_t route=0;uint32_t fee=0;uint8_t capacity=0,target=0;int32_t amount=0;bool approve=false;
        std::set<std::string> invites;std::string expectedTarget;bool staleTarget=false;
        switch(op){
        case ClientOp::Join:case ClientOp::Leave:case ClientOp::Cancel:event=r.U64();break;
        case ClientOp::Create:{route=r.U16();fee=r.U32();capacity=r.U8();unsigned n=r.U8();if(n>proto::kMaxPlayers)return;
            for(unsigned i=0;i<n&&r.Ok();++i){uint8_t id=r.U8();auto expected=r.Str(64);auto it=std::find_if(peers.begin(),peers.end(),[&](const auto& p){return p.id==id;});
                if(it==peers.end()||it->account!=expected)staleTarget=true;
                else if(!invites.insert(it->account).second)staleTarget=true;}break;}
        case ClientOp::CashChange:case ClientOp::Initialize:amount=r.I32();break;
        case ClientOp::SetCash:target=r.U8();expectedTarget=r.Str(64);amount=r.I32();if(!peer.host)return;break;
        case ClientOp::DecideCredit:credit=r.U64();approve=r.Bool();if(!peer.host)return;break;
        default:return;
        }
        if(!r.Ok()||r.Remaining())return;
        (void)route;(void)fee;(void)capacity;
        result=CashResult(candidate.cash.Sequence(peer.account,sequence));
        if(result==Error::None){
            if(staleTarget)result=Error::Missing;
            else switch(op){
            case ClientOp::Join:result=Error::Locked;break;
            case ClientOp::Leave:result=candidate.Leave(peer.account,event,now);break;
            case ClientOp::Cancel:result=candidate.Cancel(peer.account,event,now);break;
            case ClientOp::Create:result=Error::Locked;break;
            case ClientOp::CashChange:
                if(!amount||amount < -10000000||amount>10000000)result=Error::Limit;
                else result=CashResult(candidate.cash.Change(peer.account,amount,amount<0?economy::TransactionKind::Spending:economy::TransactionKind::Income));
                break;
            case ClientOp::Initialize:{auto* a=candidate.cash.Find(peer.account);
                if(!a||a->initialized||amount<0||amount>economy::kMaximumCash)result=Error::NotOpen;
                else result=CashResult(candidate.cash.Change(peer.account,amount,economy::TransactionKind::Initialize));break;}
            case ClientOp::SetCash:{auto it=std::find_if(peers.begin(),peers.end(),[&](const auto& p){return p.id==target;});
                if(it==peers.end()||it->account!=expectedTarget)result=Error::Missing;
                else if(amount<0||amount>economy::kMaximumCash)result=Error::Limit;
                else if(auto* a=candidate.cash.Find(it->account)){
                    result=CashResult(candidate.cash.Change(it->account,(int64_t)amount-a->cash,economy::TransactionKind::Administrative));
                    changedAccount=it->account;changedCash=amount;
                }
                else result=Error::Missing;break;}
            case ClientOp::DecideCredit:result=CashResult(candidate.cash.Decide(credit,approve));break;
            default:break;
            }
        }
    }
    if(!candidate.ValidCashHeadroom()){
        candidate=m_engine;
        if(requestSequence)candidate.cash.Sequence(peer.account,requestSequence);
        result=Error::Limit;
    }
    if(!Commit(std::move(candidate)))result=Error::Storage;
    Notice(peer,result);
    if(result==Error::None&&!changedAccount.empty()){
        Writer confirmed(proto::S_STREET_RACING);confirmed.U8((uint8_t)ServerOp::CashSet);confirmed.Str(changedAccount,64);confirmed.I32(changedCash);m_send(peer.id,confirmed);
    }
    // Also acknowledge rejected requests, so optimistic native debits cannot stick.
    if(op!=ClientOp::Observe&&op!=ClientOp::World)Snapshot(peer,now);
}
void StreetRacing::Tick(const std::vector<StreetPeer>& peers,uint64_t now) {
    if(now-m_lastTick<100)return;m_lastTick=now;
    auto candidate=m_engine;
    if(m_lastWorld&&now-m_lastWorld>5000){candidate.World(false,0,false,now);m_lastWorld=0;}
    candidate.Tick(now);Commit(std::move(candidate));
    if(m_revision==m_sentRevision||now-m_lastSend<100)return;
    for(const auto& peer:peers)Snapshot(peer,now);
    m_sentRevision=m_revision;m_lastSend=now;
}
bool StreetRacing::MissionReward(const StreetPeer& host,uint64_t sequence,int32_t amount,const std::vector<StreetPeer>& peers) {
    if(!host.host||amount<=0||amount>10000000)return false;
    auto candidate=m_engine;if(candidate.cash.Sequence(host.account,sequence)!=economy::CashError::None)return false;
    for(const auto& peer:peers)if(!peer.host&&peer.playing){
        int64_t reserved=0;for(const auto& [id,event]:candidate.events)for(const auto& p:event.participants)if(p.account==peer.account)reserved+=p.stake;
        auto* account=candidate.cash.Find(peer.account);if(!account)return false;
        int64_t credit=std::min<int64_t>(amount,economy::kMaximumCash-account->cash-reserved);
        if(candidate.cash.Change(peer.account,credit,economy::TransactionKind::Income)!=economy::CashError::None)return false;
    }
    return Commit(std::move(candidate));
}
}
