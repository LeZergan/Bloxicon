#pragma once
#include "util/VitaOfficialEndpoint.h"
#include <curl/curl.h>
#include <algorithm>
#include <string>

namespace RBX {
inline CURLcode performVitaHttps(CURL* curl,const std::string& url,long connectTimeoutMs)
{
    static const std::string origin=std::string("https://")+VitaOfficialEndpoint::host;
    if(url.compare(0,origin.size(),origin)!=0 || (url.size()>origin.size() && url[origin.size()]!='/'))
        return curl_easy_perform(curl);

    static const std::string route=[] {
        const auto& ip=VitaOfficialEndpoint::ipv4;
        return std::string(VitaOfficialEndpoint::host)+":443:"+std::to_string(ip[0])+"."+
            std::to_string(ip[1])+"."+std::to_string(ip[2])+"."+std::to_string(ip[3])+":443";
    }();
    auto* target=curl_slist_append(nullptr,route.c_str());
    if(!target)return CURLE_OUT_OF_MEMORY;
    auto result=curl_easy_setopt(curl,CURLOPT_CONNECT_TO,target);
    if(result==CURLE_OK){
        const long firstBudget=connectTimeoutMs>0?std::min(connectTimeoutMs,3500L):3500L;
        result=curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,firstBudget);
        if(result==CURLE_OK)result=curl_easy_perform(curl);
    }
    curl_easy_setopt(curl,CURLOPT_CONNECT_TO,static_cast<curl_slist*>(nullptr));
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,connectTimeoutMs);
    curl_slist_free_all(target);

    // Only switch routes before any application request. A timeout after a
    // request, TLS verification failure or cancellation belongs to the caller.
    if(result==CURLE_COULDNT_CONNECT || result==CURLE_OPERATION_TIMEDOUT){
        long sent=0, status=0;
        double connected=0;
        if(curl_easy_getinfo(curl,CURLINFO_REQUEST_SIZE,&sent)==CURLE_OK &&
           curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status)==CURLE_OK &&
           curl_easy_getinfo(curl,CURLINFO_CONNECT_TIME,&connected)==CURLE_OK &&
           !sent && !status && connected==0)
            return curl_easy_perform(curl);
    }
    return result;
}
}
