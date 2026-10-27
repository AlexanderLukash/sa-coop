#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "common/lib/buffer/Buffer.h"

namespace coop::economy {
constexpr int64_t kMaximumCash = 999999999;
enum class CashError : uint8_t { None, Sequence, Funds, Range, Missing, Limit };
enum class TransactionKind : uint8_t { Initialize, Spending, Income, Administrative, Stake, Refund, Prize };
struct Account {
    int64_t cash = 0;
    uint64_t sequence = 0, lastTransaction = 0;
    bool initialized = false;
};
struct Transaction {
    uint64_t id = 0, event = 0;
    std::string account;
    int64_t delta = 0;
    TransactionKind kind = TransactionKind::Initialize;
};
struct CreditRequest {
    uint64_t id = 0;
    std::string account;
    int32_t amount = 0;
};

// A native client may debit its account, but only the trusted host or a server
// rule can authorize income. No client-supplied absolute balance is reconciled
// into this ledger after initialization/migration.
class CashLedger {
public:
    std::map<std::string, Account> accounts;
    std::vector<Transaction> history;
    std::vector<CreditRequest> credits;
    uint64_t nextTransaction = 1, nextCredit = 1, revision = 0;

    Account* Find(const std::string& key) {
        auto it=accounts.find(key); return it==accounts.end()?nullptr:&it->second;
    }
    const Account* Find(const std::string& key) const {
        auto it=accounts.find(key); return it==accounts.end()?nullptr:&it->second;
    }
    bool Open(const std::string& key, int64_t initial=0, bool initialized=false) {
        if(key.empty()||key.size()>64||initial<0||initial>kMaximumCash) return false;
        if(accounts.count(key))return true;
        if(accounts.size()>=4096)return false;
        accounts.emplace(key,Account{initial,0,0,initialized});++revision;return true;
    }
    CashError Sequence(const std::string& key,uint64_t sequence) {
        Account* a=Find(key);
        if(!a)return CashError::Missing;
        if(!sequence||sequence!=a->sequence+1)return CashError::Sequence;
        a->sequence=sequence;++revision;return CashError::None;
    }
    CashError Change(const std::string& key,int64_t delta,TransactionKind kind,uint64_t event=0) {
        Account* a=Find(key);if(!a)return CashError::Missing;
        if(delta < -kMaximumCash || delta > kMaximumCash || a->cash+delta>kMaximumCash)return CashError::Range;
        if(a->cash+delta<0)return CashError::Funds;
        a->cash+=delta;a->initialized=true;a->lastTransaction=nextTransaction++;
        history.push_back({a->lastTransaction,event,key,delta,kind});
        if(history.size()>2048)history.erase(history.begin(),history.begin()+history.size()-2048);
        ++revision;return CashError::None;
    }
    CashError ProposeIncome(const std::string& key,int32_t amount) {
        if(!Find(key))return CashError::Missing;
        if(amount<=0||amount>10000000)return CashError::Range;
        if(credits.size()>=64||std::count_if(credits.begin(),credits.end(),[&](const auto& c){return c.account==key;})>=8)
            return CashError::Limit;
        credits.push_back({nextCredit++,key,amount});++revision;return CashError::None;
    }
    CashError Decide(uint64_t id,bool approve) {
        auto it=std::find_if(credits.begin(),credits.end(),[&](const auto& c){return c.id==id;});
        if(it==credits.end())return CashError::Missing;
        CashError result=CashError::None;
        if(approve)result=Change(it->account,it->amount,TransactionKind::Income);
        if(result==CashError::None){credits.erase(it);++revision;}
        return result;
    }
    CashError CreditPendingIncome() {
        while(!credits.empty()) {
            CashError result=Decide(credits.front().id,true);
            if(result!=CashError::None)return result;
        }
        return CashError::None;
    }
    void Write(Writer& w) const {
        w.U64(nextTransaction);w.U64(nextCredit);w.U32((uint32_t)accounts.size());
        for(const auto& [key,a]:accounts){w.Str(key,64);w.U64((uint64_t)a.cash);w.U64(a.sequence);w.U64(a.lastTransaction);w.Bool(a.initialized);}
        w.U16((uint16_t)credits.size());
        for(const auto& c:credits){w.U64(c.id);w.Str(c.account,64);w.I32(c.amount);}
        w.U16((uint16_t)history.size());
        for(const auto& t:history){w.U64(t.id);w.U64(t.event);w.Str(t.account,64);w.U64((uint64_t)t.delta);w.U8((uint8_t)t.kind);}
    }
    bool Read(Reader& r) {
        CashLedger loaded;loaded.nextTransaction=r.U64();loaded.nextCredit=r.U64();uint32_t n=r.U32();
        if(!loaded.nextTransaction||!loaded.nextCredit||n>4096)return false;
        for(uint32_t i=0;i<n&&r.Ok();++i){
            auto key=r.Str(64);Account a;a.cash=(int64_t)r.U64();a.sequence=r.U64();a.lastTransaction=r.U64();a.initialized=r.Bool();
            if(key.empty()||a.cash<0||a.cash>kMaximumCash||a.lastTransaction>=loaded.nextTransaction||!loaded.accounts.emplace(key,a).second)return false;
        }
        n=r.U16();if(n>64)return false;
        for(uint32_t i=0;i<n&&r.Ok();++i){CreditRequest c;c.id=r.U64();c.account=r.Str(64);c.amount=r.I32();
            if(!c.id||c.id>=loaded.nextCredit||!loaded.Find(c.account)||c.amount<=0||c.amount>10000000||
               std::any_of(loaded.credits.begin(),loaded.credits.end(),[&](const auto& other){return other.id==c.id;}))return false;
            loaded.credits.push_back(c);
        }
        n=r.U16();if(n>2048)return false;uint64_t previous=0;
        for(uint32_t i=0;i<n&&r.Ok();++i){Transaction t;t.id=r.U64();t.event=r.U64();t.account=r.Str(64);t.delta=(int64_t)r.U64();t.kind=(TransactionKind)r.U8();
            if(!t.id||t.id<=previous||t.id>=loaded.nextTransaction||!loaded.Find(t.account)||t.delta < -kMaximumCash||t.delta>kMaximumCash||
               (uint8_t)t.kind>(uint8_t)TransactionKind::Prize)return false;
            previous=t.id;loaded.history.push_back(t);
        }
        if(!r.Ok())return false;*this=std::move(loaded);return true;
    }
};
}
