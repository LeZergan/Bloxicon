#pragma once
#include <algorithm>
#include <cstdint>
#include <chrono>
#include <sstream>
#include <string>
namespace VitaBrowser {
inline uint64_t artworkNow() {
    return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline std::string artworkKey(const std::string& identity) {
    uint64_t hash=14695981039346656037ULL;
    for(unsigned char c:identity){hash^=c;hash*=1099511628211ULL;}
    std::ostringstream text;text<<std::hex<<hash;return text.str();
}
struct ArtworkRetry {
    unsigned attempts=0;
    uint64_t next=0;
    bool complete=false;
    bool ready(uint64_t now)const{return !complete&&attempts<4&&now>=next;}
    void queued(){++attempts;}
    void busy(uint64_t now){next=now+500000;}
    void failed(uint64_t now){const uint64_t delays[]={2000000,8000000,30000000,120000000};next=now+delays[attempts?std::min(attempts-1,3u):0];}
    void succeeded(){complete=true;}
    void later(uint64_t now){complete=false;attempts=0;next=now+60000000;}
};
}
