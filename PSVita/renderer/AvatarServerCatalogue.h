#pragma once
#include "AvatarCatalogMerge.h"
#include "AvatarRuntime.h"
#include "VitaAccount.h"
#include <mutex>

namespace VitaAvatar {

struct CatalogueSession
{
    std::string origin, token;

    bool acquire() { return VitaAccount::connector().communitySession(origin, token); }

    std::string get(const std::string& path, size_t limit, const std::atomic<bool>& cancel) const
    {
        auto response = VitaAccount::transferResponse(origin, path.c_str(), {}, token, cancel, {}, false, limit);
        if(response.status != 200)
            throw std::runtime_error("The server wardrobe could not deliver this file.");
        return std::move(response.bytes);
    }
};

inline bool fetchCatalogueBlob(const CatalogueSession& session, const Blob& blob,
    std::string& bytes, const std::atomic<bool>& cancel)
{
    if(session.token.empty() || !sha256Text(blob.sha256) || !blob.bytes || blob.bytes > kMaxBlobBytes)
        return false;
    try {
        auto payload = session.get("/api/v1/wardrobe/native/blob/" + blob.sha256, blob.bytes, cancel);
        if(cancel.load() || payload.size() != blob.bytes || sha256(payload) != blob.sha256)
            return false;
        bytes.swap(payload);
        return true;
    } catch(const std::exception&) { return false; }
}

inline bool createCatalogueCacheDirectories(const std::string& path)
{
    // Runtime packs synthesize directories for normal stat(). The physical
    // directories must also exist before an on-demand file can be written.
    const std::string root = "ux0:/data/RBXV00001/";
    for(auto slash=path.find('/',root.size());slash!=std::string::npos;slash=path.find('/',slash+1)) {
        const auto directory=path.substr(0,slash);
        SceIoStat status={};
        if(sceIoMkdir(directory.c_str(),0777)<0 &&
            (sceIoGetstat(directory.c_str(),&status)<0 || !SCE_S_ISDIR(status.st_mode)))
            return false;
    }
    return true;
}

inline bool publishCatalogueCache(const std::string& path, const std::string& bytes)
{
    if(!createCatalogueCacheDirectories(path))return false;
    SceIoStat physical={};
    if(!vita_runtime_pack_has_file(path.c_str()) || sceIoGetstat(path.c_str(),&physical)>=0)
        return VitaBrowser::writeAtomic(path,bytes);
    // A virtual asset has no physical file to rename to a backup. Install
    // the verified loose override while leaving the immutable pack intact.
    const auto staged=path+".catalogue-download";
    if(!VitaBrowser::writeAtomic(staged,bytes))return false;
    if(sceIoRename(staged.c_str(),path.c_str())>=0)return true;
    std::remove(staged.c_str());
    return false;
}

inline OutfitBlobReader serverOutfitReader()
{
    if(!VitaBrowser::serverCataloguesEnabled())return {};
    CatalogueSession session;
    session.acquire();
    return [session](const Blob& blob, std::string& bytes, const std::atomic<bool>& cancel) {
        if(cancel.load() || !safePath(blob.path))return false;
        const bool alias = blob.path.compare(0,8,"aliases/")==0;
        const std::string relative = alias ? blob.path.substr(8) : blob.path;
        if(alias && (relative.empty() || relative.find_first_not_of("0123456789")!=std::string::npos))
            return false;
        const std::string path = (alias ? "ux0:/data/RBXV00001/assets/" : dataRoot()) + relative;
        // Keep the original renderer paths. Publish only files verified against
        // the server catalogue so the engine and the outfit serializer see the same bytes.
        static std::mutex cacheMutex;
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            if(readBounded(path,blob.bytes,bytes) && bytes.size()==blob.bytes && sha256(bytes)==blob.sha256)
                return true;
        }
        if(!fetchCatalogueBlob(session,blob,bytes,cancel))return false;
        std::lock_guard<std::mutex> lock(cacheMutex);
        if(cancel.load())return false;
        return publishCatalogueCache(path,bytes);
    };
}

class ServerCatalogue
{
    using Snapshot = std::shared_ptr<const Catalog>;
    VitaBrowser::LoaderAsyncTask<Snapshot> task_;
    Snapshot catalog_;
    std::string account_;
    uint64_t retryAfter_ = 0;
    bool pending_ = false;

public:
    void tick()
    {
        if(!VitaBrowser::serverCataloguesEnabled())return;
        const auto& account = VitaAccount::connector().accountId();
        if(account_ != account) {
            task_.cancel();
            pending_ = false;
            catalog_.reset();
            account_ = account;
            retryAfter_ = 0;
        }
        if(pending_) {
            std::string error;
            const auto state = task_.poll(catalog_,error);
            if(state == decltype(task_)::Pending)return;
            pending_ = false;
            retryAfter_ = sceKernelGetSystemTimeWide() + 5000000;
        }
        if(catalog_ || !task_.settled() || sceKernelGetSystemTimeWide() < retryAfter_)return;
        CatalogueSession session;
        if(!session.acquire())return;
        pending_ = task_.start(VitaAccount::scheduleCommunity, [session](const std::atomic<bool>& cancel) {
            Catalog combined;
            for(const auto* name : {"wardrobe.v1", "bodies.v1"}) {
                auto bytes = session.get(std::string("/api/v1/wardrobe/native/index/") + name, kMaxCatalogBytes, cancel);
                Catalog next;
                std::string error;
                if(!parseCatalog(bytes,next,error) || !mergeCatalog(combined,next,error))
                    throw std::runtime_error(error);
            }
            if(combined.items.empty())throw std::runtime_error("The server wardrobe is empty.");
            return std::make_shared<const Catalog>(std::move(combined));
        });
        if(!pending_)retryAfter_ = sceKernelGetSystemTimeWide() + 5000000;
    }

    bool read(Snapshot& output, std::string& error)
    {
        tick();
        if(catalog_) {
            output = catalog_;
            error.clear();
            return true;
        }
        error = account_.empty() ? "Sign in to load the server wardrobe." :
            pending_ ? "Loading the server wardrobe. Open Avatar again shortly." :
            "The server wardrobe is unavailable. Try again shortly.";
        return false;
    }
};

inline ServerCatalogue& serverCatalogue()
{
    static ServerCatalogue catalogue;
    return catalogue;
}

} // namespace VitaAvatar
