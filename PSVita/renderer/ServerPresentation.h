#pragma once
#include "ServerEndpoint.h"
#include "ServerProbeState.h"
#include <cctype>

namespace VitaAdhoc {
// Display-only redaction: never modify actual connection endpoints or packets.
inline std::string privateServerText(const std::string& source) {
    std::string result;
    for(size_t i=0;i<source.size() && result.size()<160;) {
        const unsigned char c=source[i];
        if(c>='0' && c<='9') {
            size_t end=i;unsigned dots=0;
            while(end<source.size() && ((source[end]>='0'&&source[end]<='9') || source[end]=='.')) {
                dots+=source[end]=='.';++end;
            }
            if(dots==3) {result+="[address hidden]";i=end;continue;}
        }
        if(c<32 || c==127)result+=' ';
        else result+=char(c);
        ++i;
    }
    // Server advertisements need a short name, never a URL, email or path.
    if(result.find('@')!=std::string::npos || result.find("://")!=std::string::npos ||
       result.find('\\')!=std::string::npos || result.find('/')!=std::string::npos)
        return "Custom server";
    return VitaText::bounded(result,160);
}
inline std::string serverDisplayName(const ServerEndpoint& endpoint,size_t row) {
    if(!endpoint.name.empty())return privateServerText(endpoint.name);
    if(endpoint.host=="bloxicon.example.invalid")return "Official hosting";
    return "Custom server "+std::to_string(row+1);
}
inline std::string serverAddressHint(const ServerEndpoint& endpoint) {
    return "Address hidden  /  Port "+std::to_string(endpoint.port);
}
}
