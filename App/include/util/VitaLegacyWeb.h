#pragma once
#include <string>
#include <algorithm>
namespace RBX { namespace VitaLegacyWeb {
// Only the retired public asset metadata route. Purchases, ownership, private
// assets and user-authenticated requests retain their original failure rules.
inline std::string publicProductInfo(const std::string& url, bool post) {
    if(post)return {};
    for(const char* prefix:{"https://api.roblox.com/marketplace/productinfo?assetId=",
                            "http://api.roblox.com/marketplace/productinfo?assetId="}) {
        const std::string base(prefix);
        if(url.compare(0,base.size(),base))continue;
        const std::string id=url.substr(base.size());
        if(id.empty()||id.size()>16||id[0]=='0'||
           !std::all_of(id.begin(),id.end(),[](unsigned char c){return c>='0'&&c<='9';}))return {};
        return "https://economy.roblox.com/v2/assets/"+id+"/details";
    }
    return {};
}
} }
