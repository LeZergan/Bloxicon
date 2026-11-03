#pragma once
#include <curl/curl.h>
#include <cstddef>

namespace RBX {
enum class VitaCurlChannel : unsigned { Account, Engine, Public, Count };
// Cache belongs to one worker, with distinct authenticated/public transports.
// Taking a slot removes it, so a nested request never resets an active handle.
struct VitaCurlConnections {
    CURL* idle[static_cast<unsigned>(VitaCurlChannel::Count)]{};
    ~VitaCurlConnections(){for(auto* handle:idle)if(handle)curl_easy_cleanup(handle);}
};
inline VitaCurlConnections& vitaCurlConnections(){static thread_local VitaCurlConnections cache;return cache;}
inline CURL* acquireVitaCurlConnection(VitaCurlChannel channel){
    auto& slot=vitaCurlConnections().idle[static_cast<unsigned>(channel)];
    CURL* result=slot;slot=nullptr;
    if(!result)result=curl_easy_init();
    if(result){
        curl_easy_reset(result);
        curl_easy_setopt(result,CURLOPT_MAXCONNECTS,2L);
        curl_easy_setopt(result,CURLOPT_DNS_CACHE_TIMEOUT,300L);
        curl_easy_setopt(result,CURLOPT_TCP_NODELAY,1L);
        curl_easy_setopt(result,CURLOPT_TCP_KEEPALIVE,1L);
    }
    return result;
}
inline void releaseVitaCurlConnection(CURL* handle,VitaCurlChannel channel){
    if(!handle)return;
    // reset preserves shares: explicitly release them before their owner's
    // lifetime ends. Callers finish cookie-jar writes before returning a lease.
    curl_easy_setopt(handle,CURLOPT_SHARE,static_cast<CURLSH*>(nullptr));
    curl_easy_reset(handle);
    auto& slot=vitaCurlConnections().idle[static_cast<unsigned>(channel)];
    if(slot)curl_easy_cleanup(handle);else slot=handle;
}
}
