#pragma once
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <dirent.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <utime.h>
#include <vector>

namespace VitaBrowser { namespace GalleryCache {
enum Kind { Images, Manifests };
constexpr size_t imageQuota=64u*1024u*1024u,manifestQuota=4u*1024u*1024u;
inline const char* directory(Kind kind){return kind==Images?
    "ux0:/data/Bloxicon/system/thumbnails":"ux0:/data/Bloxicon/system/game-info";}
inline size_t quota(Kind kind){return kind==Images?imageQuota:manifestQuota;}
// Match only names this loader generates. No originals, other artwork, user
// files, temporary downloads or rollback files are eligible for eviction.
inline bool positiveId(const std::string& text,long long& value){
    if(text.empty()||text.size()>19||text[0]=='0')return false;
    unsigned long long parsed=0;
    for(char c:text){if(c<'0'||c>'9'||parsed>(9223372036854775807ull-unsigned(c-'0'))/10)return false;
        parsed=parsed*10+unsigned(c-'0');}
    value=static_cast<long long>(parsed);return true;
}
inline bool managed(const std::string& name,Kind kind,long long& universe){
    if(name.compare(0,8,"gallery-")!=0)return false;
    const std::string suffix=kind==Images?".png":".json";
    if(name.size()<=8+suffix.size()||name.compare(name.size()-suffix.size(),suffix.size(),suffix)!=0)return false;
    const std::string ids=name.substr(8,name.size()-8-suffix.size());
    if(kind==Manifests)return positiveId(ids,universe);
    const auto split=ids.find('-');long long image=0;
    return split!=std::string::npos&&positiveId(ids.substr(0,split),universe)&&positiveId(ids.substr(split+1),image);
}
struct State {
    std::mutex mutex;
    std::map<long long,std::shared_ptr<std::atomic<size_t>>> pins;
    std::atomic<size_t> pending[2]={{0},{0}};
};
// Optional executors can finish work during application teardown.
inline State& state(){static State* value=new State;return *value;}
struct Lease {
    const long long universe;
    std::shared_ptr<std::atomic<size_t>> count;
    // Acquired on I/O workers; released by either a worker or the UI. UI exits
    // must never wait for the cache scan's mutex or touch the filesystem.
    explicit Lease(long long value):universe(value){
        auto& s=state();std::lock_guard<std::mutex> lock(s.mutex);
        for(auto i=s.pins.begin();i!=s.pins.end();){
            if(!i->second->load(std::memory_order_acquire))i=s.pins.erase(i);else ++i;
        }
        auto& counter=s.pins[universe];if(!counter)counter=std::make_shared<std::atomic<size_t>>(0);
        count=counter;count->fetch_add(1,std::memory_order_relaxed);
    }
    ~Lease(){count->fetch_sub(1,std::memory_order_release);}
    Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
};
inline std::shared_ptr<Lease> lease(long long universe){return universe>0?std::make_shared<Lease>(universe):nullptr;}
struct Reservation {
    const Kind kind;const size_t bytes;
    Reservation(Kind k,size_t b):kind(k),bytes(b){}
    ~Reservation(){state().pending[kind].fetch_sub(bytes,std::memory_order_release);}
    Reservation(const Reservation&)=delete;Reservation& operator=(const Reservation&)=delete;
};
inline void touch(const std::string& path){::utime(path.c_str(),nullptr);}
// Scan/trim only on the optional I/O worker. Reservation lives through active
// screen publication, so queued results cannot all claim the same free space.
// An open game's entire gallery is pinned, including thumbnails still decoding.
inline std::shared_ptr<Reservation> reserve(Kind kind,const std::string& target,size_t incoming,
    const std::atomic<bool>& cancelled){
    auto& s=state();std::lock_guard<std::mutex> lock(s.mutex);
    const size_t cap=quota(kind),pending=s.pending[kind].load(std::memory_order_acquire);
    if(cancelled.load(std::memory_order_acquire)||incoming>cap||pending>cap-incoming)return nullptr;
    struct Entry {std::string path;size_t bytes;time_t used;long long universe;};
    std::vector<Entry> entries;size_t total=0;
    DIR* scan=::opendir(directory(kind));
    if(!scan)return nullptr;
    while(dirent* item=::readdir(scan)){
        long long universe=0;if(!managed(item->d_name,kind,universe))continue;
        const std::string path=std::string(directory(kind))+"/"+item->d_name;struct stat info;
        if(::stat(path.c_str(),&info)!=0||!S_ISREG(info.st_mode)||info.st_size<0)continue;
        const auto bytes=static_cast<unsigned long long>(info.st_size);
        if(entries.size()==4096||bytes>size_t(-1)-total||cancelled.load(std::memory_order_acquire)){::closedir(scan);return nullptr;}
        total+=size_t(bytes);entries.push_back({path,size_t(bytes),info.st_mtime,universe});
    }
    ::closedir(scan);
    std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.used!=b.used?a.used<b.used:a.path<b.path;});
    const size_t allowed=cap-pending-incoming;
    for(const auto& entry:entries){
        if(total<=allowed)break;
        if(cancelled.load(std::memory_order_acquire))return nullptr;
        const auto pin=s.pins.find(entry.universe);
        if(entry.path==target||(pin!=s.pins.end()&&pin->second->load(std::memory_order_acquire)))continue;
        if(std::remove(entry.path.c_str())==0)total-=entry.bytes;
    }
    if(total>allowed||cancelled.load(std::memory_order_acquire))return nullptr;
    auto reservation=std::make_shared<Reservation>(kind,incoming);
    s.pending[kind].fetch_add(incoming,std::memory_order_relaxed);return reservation;
}
} }
