#pragma once

#include "OriginalPlaceIntegrity.h"
#include "RuntimePack.h"
#include "VitaAccount.h"
#include <mutex>
#include <sstream>

namespace VitaAvatar {

enum class AvatarPackSelection { Essential, Additions, Full };

struct AvatarPackProgress
{
    enum Phase { Checking, Downloading, Verifying, Finishing };
    std::atomic<Phase> phase{Checking};
    std::atomic<unsigned> parts{0}, verified{0}, current{0}, retained{0};
    std::atomic<uint64_t> total{0}, ready{0}, transferred{0}, transferStarted{0};
};

inline bool scheduleAvatarPack(std::function<void()> work)
{
    static RBX::ThreadPool pool(1,RBX::BaseThreadPool::WaitForRunningTasks,1);
    return pool.schedule([work](boost::shared_ptr<rbx::spin_mutex>){work();});
}

// The loader owns session rotation. The download worker only takes snapshots.
class AvatarPackAuthorization
{
    std::mutex mutex;
    std::string bearer;
    bool refreshRequested=false;
public:
    void publish(std::string token)
    {
        std::lock_guard<std::mutex> lock(mutex);
        bearer=std::move(token);
    }

    void requestRefresh(const std::string& rejected)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(bearer==rejected)refreshRequested=true;
    }

    bool takeRefreshRequest()
    {
        std::lock_guard<std::mutex> lock(mutex);
        const bool requested=refreshRequested;
        refreshRequested=false;
        return requested;
    }

    std::string wait(const std::atomic<bool>& cancel,const std::string& rejected={})
    {
        const auto deadline=sceKernelGetSystemTimeWide()+30000000;
        while(!cancel.load()&&sceKernelGetSystemTimeWide()<deadline){
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(!bearer.empty()&&bearer!=rejected)return bearer;
            }
            sceKernelDelayThread(100000);
        }
        VitaAccount::require(!cancel.load(),"Avatar installation cancelled.");
        throw std::runtime_error("Account session unavailable. Retry to continue the installation.");
    }
};

inline const char* avatarPackDirectory() { return "ux0:/data/Bloxicon/system/avatar-packs/"; }

inline std::string avatarPackGroupDirectory(const char* group) { return std::string(avatarPackDirectory())+group+"/"; }

inline std::string avatarPackMarker(const char* group) { return avatarPackGroupDirectory(group)+"installed.v1"; }

inline std::vector<std::string> installedAvatarPacks(const char* group)
{
    const auto marker=avatarPackMarker(group);
    VitaBrowser::recoverAtomicTarget(marker);
    std::string list;
    if(!readBounded(marker,4096,list)){
        const auto legacyMarker=std::string(avatarPackDirectory())+group+".v1";
        VitaBrowser::recoverAtomicTarget(legacyMarker);
        if(!readBounded(legacyMarker,4096,list))return {};
    }
    std::istringstream lines(list);
    std::string digest;
    std::vector<std::string> result;
    while(lines>>digest){
        if(!sha256Text(digest)||result.size()==32)return {};
        auto path=avatarPackGroupDirectory(group)+digest+".pac";
        if(!VitaBrowser::fileExists(path)){
            path=std::string(avatarPackDirectory())+digest+".pac";
            if(!VitaBrowser::fileExists(path))return {};
        }
        result.push_back(std::move(path));
    }
    return result;
}

struct AvatarPackPart { std::string name,digest; unsigned bytes; };

inline std::vector<AvatarPackPart> avatarPackParts(const rapidjson::Value& manifest,const char* group)
{
    using VitaAccount::require;
    require(manifest.HasMember(group)&&manifest[group].IsArray()&&!manifest[group].Empty()&&manifest[group].Size()<=32,
        "Invalid avatar pack list.");
    std::vector<AvatarPackPart> result;
    const auto& parts=manifest[group];
    for(rapidjson::SizeType index=0;index<parts.Size();++index){
        const auto& row=parts[index];
        const unsigned part=index+1;
        const std::string name=VitaAccount::field(row,"file"),digest=VitaAccount::field(row,"sha256");
        const auto number=std::to_string(part);
        require(name==std::string("avatar-")+group+"-"+(part<10?"0":"")+number+".pac"&&sha256Text(digest)&&
            row.HasMember("bytes")&&row["bytes"].IsUint()&&row["bytes"].GetUint()>16&&row["bytes"].GetUint()<=160u*1024*1024,
            "Invalid avatar pack metadata.");
        result.push_back({name,digest,row["bytes"].GetUint()});
    }
    return result;
}

inline void clearPackStage(const std::string& path)
{
    VitaAccount::require(std::remove(path.c_str())==0 || !VitaBrowser::fileExists(path),
        "Could not clear an invalid avatar download.");
}

inline void downloadAvatarPackPart(const std::string& origin,AvatarPackAuthorization& authorization,
    const AvatarPackPart& part,const std::string& temporary,const std::atomic<bool>& cancel,AvatarPackProgress& progress)
{
    using VitaAccount::require;
    const auto tagPath=temporary+".sha256";
    const auto expectedTag=part.digest+'\n';
    std::string tag;
    VitaBrowser::recoverAtomicTarget(tagPath);
    if(!readBounded(tagPath,80,tag) || tag!=expectedTag){
        clearPackStage(temporary);
        require(VitaBrowser::writeAtomic(tagPath,expectedTag),"Could not save avatar download progress.");
    }

    auto token=authorization.wait(cancel);
    const auto route="/api/v1/wardrobe/native/pack/"+part.name;
    bool restarted=false;
    for(unsigned attempt=0;attempt<3;++attempt){
        require(!cancel.load(),"Avatar installation cancelled.");
        const auto staged=VitaBrowser::fileSize(temporary);
        if(staged==part.bytes)return;
        if(staged>part.bytes)clearPackStage(temporary);
        const unsigned offset=staged<part.bytes?unsigned(staged):0;
        unsigned long long available=0;
        require(!VitaBrowser::ux0FreeBytes(available) || available>=part.bytes-offset+4u*1024*1024,
            "Insufficient Vita storage for this part. Free space and retry.");

        progress.retained=offset;
        progress.current=0;
        progress.transferStarted=sceKernelGetSystemTimeWide();
        progress.phase=AvatarPackProgress::Downloading;
        VitaAccount::Response response{};
        try{
            std::unique_ptr<FILE,decltype(&std::fclose)> file(
                std::fopen(temporary.c_str(),offset?"ab":"wb"),std::fclose);
            require(bool(file),"Could not write avatar pack. Check free storage.");
            VitaAccount::PackOutput sink{file.get(),offset,part.bytes,progress.current};
            response=VitaAccount::transferResponse(origin,route.c_str(),{},token,cancel,{},false,
                part.bytes-offset,900000L,&sink);
            require(std::fclose(file.release())==0,"Could not save avatar pack. Check free storage.");
        }catch(const VitaAccount::NetworkError&){
            if(cancel.load() || attempt==2)throw;
        }
        const auto added=progress.current.exchange(0);
        progress.transferred.fetch_add(added);
        progress.retained=offset+added;
        progress.phase=AvatarPackProgress::Checking;
        if(!response.rejectedFile && response.status==(offset?206:200) && VitaBrowser::fileSize(temporary)==part.bytes)
            return;
        if(response.status==401 && attempt<2){
            authorization.requestRefresh(token);
            token=authorization.wait(cancel,token);
        }else if(offset && !restarted && response.rejectedFile &&
                 (response.status==200 || response.status==206 || response.status==416)){
            clearPackStage(temporary);
            progress.retained=0;
            restarted=true;
        }else if(response.status && response.status!=408 && response.status!=429 && response.status<500){
            throw std::runtime_error("The server rejected this avatar pack part.");
        }
        if(response.retry>3)
            throw std::runtime_error("The server is busy. Retry the download in "+std::to_string(response.retry)+" seconds.");
        if(attempt<2)sceKernelDelayThread(std::max(500000u,response.retry*1000000u));
    }
    throw std::runtime_error("Connection interrupted. Select this pack again to resume.");
}

inline void installAvatarPackGroup(const std::string& origin,AvatarPackAuthorization& authorization,
    const std::vector<AvatarPackPart>& parts,const char* group,const std::atomic<bool>& cancel,
    AvatarPackProgress& progress)
{
    using VitaAccount::require;
    const std::string root=avatarPackGroupDirectory(group);
    sceIoMkdir(avatarPackDirectory(),0777);
    sceIoMkdir(root.c_str(),0777);
    std::string installed;
    for(const auto& part:parts){
        require(!cancel.load(),"Avatar installation cancelled.");
        progress.phase=AvatarPackProgress::Checking;
        const auto size=part.bytes;
        const auto& digest=part.digest;
        const std::string path=root+digest+".pac", temporary=root+"part.incoming";
        auto correct=[&](const std::string& file){
            const auto actual=VitaOriginalIntegrity::read(file);
            return actual.ok&&actual.bytes==size&&actual.sha256==digest;
        };
        const std::string legacy=std::string(avatarPackDirectory())+digest+".pac";
        if(!correct(path)&&(VitaBrowser::fileExists(path)||!correct(legacy))){
            downloadAvatarPackPart(origin,authorization,part,temporary,cancel,progress);
            progress.retained=size;
            progress.phase=AvatarPackProgress::Verifying;
            require(!cancel.load(),"Avatar installation cancelled.");
            if(!correct(temporary)){
                clearPackStage(temporary);
                throw std::runtime_error("Avatar pack checksum failed. Select the pack again to download a clean copy.");
            }
            require(!cancel.load() && VitaBrowser::commitAtomicTemp(path,temporary),"Could not install verified avatar pack.");
        }
        installed+=digest+'\n';
        progress.current=0;
        progress.retained=0;
        progress.ready.fetch_add(size);
        ++progress.verified;
    }
    progress.phase=AvatarPackProgress::Finishing;
    require(!cancel.load()&&VitaBrowser::writeAtomic(avatarPackMarker(group),installed),
        "Could not save the installed avatar pack list.");
}

inline std::string installAvatarPacks(const std::string& origin,AvatarPackAuthorization& authorization,
    AvatarPackSelection selection,const std::atomic<bool>& cancel,AvatarPackProgress& progress)
{
    const auto fetch=[&](const std::string& token){
        return VitaAccount::transferResponse(origin,"/api/v1/wardrobe/native/pack/manifest.json",{},token,cancel,{},false,32768);
    };
    const auto token=authorization.wait(cancel);
    auto response=fetch(token);
    if(response.status==401){
        authorization.requestRefresh(token);
        response=fetch(authorization.wait(cancel,token));
    }
    VitaAccount::require(response.status==200,"Avatar packs are unavailable. Sign in and retry.");
    const auto manifest=VitaAccount::parse(response.bytes);
    VitaAccount::require(manifest.HasMember("version")&&manifest["version"].IsInt()&&manifest["version"].GetInt()==1,
        "Unsupported avatar pack list.");
    const auto essential=selection==AvatarPackSelection::Additions?std::vector<AvatarPackPart>():avatarPackParts(manifest,"essential");
    const auto additions=selection==AvatarPackSelection::Essential?std::vector<AvatarPackPart>():avatarPackParts(manifest,"additions");
    uint64_t total=0;
    for(const auto* group:{&essential,&additions})for(const auto& part:*group)total+=part.bytes;
    progress.parts=unsigned(essential.size()+additions.size());
    progress.total=total;
    if(!essential.empty())installAvatarPackGroup(origin,authorization,essential,"essential",cancel,progress);
    if(!additions.empty())installAvatarPackGroup(origin,authorization,additions,"additions",cancel,progress);
    return "Avatar packs installed. Restart Bloxicon to use them offline.";
}

} // namespace VitaAvatar
