#pragma once
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <string>
#include <vector>
namespace VitaBrowser {
struct PackResult {std::string path;bool loaded;};
inline std::vector<PackResult>& packResults(){static std::vector<PackResult> value;return value;}
inline std::vector<std::string> userPacks() {
    std::vector<std::string> names;
    // Legacy imports still load; the clearly named folder wins on conflicts.
    for(const char* folder:{"ux0:/data/RBXV00001/packs","ux0:/data/RBXV00001/accessories"}) {
    size_t added=0;
    if(DIR* dir=opendir(folder)) {
        while(const auto* entry=readdir(dir)) {
            const std::string name=entry->d_name;
            std::string extension=name.size()>4?name.substr(name.size()-4):std::string();
            std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(name.size()>4 && name.size()<=120 && extension==".pac" && name.find_first_of("/\\:\r\n")==std::string::npos)
                {names.push_back(std::string(folder)+"/"+name);++added;}
            if(added==32)break;
        }closedir(dir);
    }
    }
    // Sort within each folder, retaining accessories-after-legacy precedence.
    std::stable_sort(names.begin(),names.end(),[](const std::string& a,const std::string& b){
        const bool aa=a.find("/accessories/")!=std::string::npos,bb=b.find("/accessories/")!=std::string::npos;
        return aa==bb?a<b:!aa;
    });return names;
}
}
