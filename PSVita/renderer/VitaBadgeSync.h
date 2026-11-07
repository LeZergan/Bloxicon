#pragma once
#include "Utf8Text.h"
#include "VitaAccount.h"
#include "util/VitaBadgeStore.h"

namespace VitaBrowser {

class BadgeSync
{
    using Award=RBX::VitaBadges::Award;
    LoaderAsyncTask<bool> task;
    std::string account;
    uint64_t due=0;
    unsigned revision_=0;
    bool pending=false, synced=false;

    static std::vector<Award> readCloud(const std::string& origin,const std::string& token,
        const std::string& account,const std::atomic<bool>& cancel,const std::string& body={})
    {
        const auto reply=VitaAccount::transferResponse(origin,"/api/v1/vita/badges",body,token,cancel,{},!body.empty(),2*1024*1024,12000L);
        VitaAccount::require(reply.status==200,"Badge history unavailable");
        const auto data=VitaAccount::parse(reply.bytes);
        VitaAccount::require(VitaAccount::field(data,"account_id")==account && data.HasMember("awards") &&
            data["awards"].IsArray() && data["awards"].Size()<=2048,"Invalid badge history");
        std::vector<Award> awards;
        for(auto entry=data["awards"].Begin();entry!=data["awards"].End();++entry){
            const auto& row=*entry;
            const auto place=VitaAccount::field(row,"place");
            VitaAccount::require(row.HasMember("id") && row["id"].IsInt() && row["id"].GetInt()>0 &&
                row.HasMember("place_id") && row["place_id"].IsInt() && row["place_id"].GetInt()>=0 &&
                row.HasMember("earned") && row["earned"].IsInt64() && row["earned"].GetInt64()>=0,
                "Invalid badge record");
            awards.push_back({row["id"].GetInt(),row["place_id"].GetInt(),row["earned"].GetInt64(),
                RBX::VitaBadges::clean(VitaText::bounded(place,200))});
        }
        return awards;
    }

public:
    void tick()
    {
        auto& connector=VitaAccount::connector();
        if(account!=connector.accountId()){
            task.cancel();
            account=connector.accountId();
            due=0;
            pending=synced=false;
            ++revision_;
        }
        if(account.empty())return;
        const auto now=sceKernelGetSystemTimeWide();
        if(pending){
            bool caughtUp=false;
            std::string error;
            const auto state=task.poll(caughtUp,error);
            if(state==decltype(task)::Pending)return;
            pending=false;
            synced=state==decltype(task)::Ready;
            ++revision_;
            due=now+(synced && caughtUp?60000000:8000000);
        }
        if(now<due || !task.settled())return;
        std::string origin,token;
        if(!connector.communitySession(origin,token))return;
        const auto name=connector.username(), id=account;
        pending=task.start([](std::function<void()> work){
            static RBX::ThreadPool pool(1,RBX::BaseThreadPool::WaitForRunningTasks,1);
            return pool.schedule([work](boost::shared_ptr<rbx::spin_mutex>){work();});
        },[origin,token,name,id](const std::atomic<bool>& cancel){
            using namespace RBX::VitaBadges;
            auto cloud=readCloud(origin,token,id,cancel);
            const auto key=accountProfile(id);
            std::vector<Award> local, legacy;
            VitaAccount::require(readProfile(key,local),"Could not read the local badge history");
            if(readProfile(name,legacy))local.insert(local.end(),legacy.begin(),legacy.end());
            std::set<int> known;
            for(const auto& award:cloud)known.insert(award.id);
            VitaAccount::Doc update;
            update.SetObject();
            rapidjson::Value rows(rapidjson::kArrayType);
            bool more=false;
            for(const auto& award:local){
                if(!known.insert(award.id).second)continue;
                if(rows.Size()==8){more=true;break;} // Stay below the account API's 4 KiB body limit.
                rapidjson::Value row(rapidjson::kObjectType);
                auto& allocator=update.GetAllocator();
                row.AddMember("id",award.id,allocator);
                row.AddMember("place_id",std::max(0,award.placeId),allocator);
                row.AddMember("earned",int64_t(std::min(award.earned,253402300799LL)),allocator);
                const auto place=VitaText::bounded(award.place,200);
                rapidjson::Value text(place.c_str(),allocator);
                row.AddMember("place",text,allocator);
                rows.PushBack(row,allocator);
            }
            if(!rows.Empty()){
                update.AddMember("awards",rows,update.GetAllocator());
                cloud=readCloud(origin,token,id,cancel,VitaAccount::json(update));
            }
            if(cancel.load())return false;
            sceIoMkdir("ux0:/data/Bloxicon/system/badges",0777);
            VitaAccount::require(mergeProfile(key,cloud),"Could not save account badge history");
            return !more || cloud.size()==2048;
        });
        if(!pending)due=now+8000000;
    }

    unsigned revision() const { return revision_; }
    const char* status() const {
        return account.empty()?"Local badge history":pending?"Syncing Bloxicon account...":
            synced?"Bloxicon account history":"Account sync unavailable. Showing saved badges.";
    }
};

inline BadgeSync& badgeSync(){static BadgeSync sync;return sync;}

} // namespace VitaBrowser
