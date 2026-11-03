#pragma once
#include <string>
#include <ctime>
namespace VitaBrowser {
inline bool hostedHex64(const std::string& value){return value.size()==64&&value.find_first_not_of("0123456789abcdef")==std::string::npos;}
// Public artwork is addressed by BOTH original-file and image-content hashes.
inline bool hostedArtworkPath(const std::string& path,const std::string& original={}){
    const std::string prefix="/catalogue-art/";
    if(path.compare(0,prefix.size(),prefix)||path.size()!=prefix.size()+64+1+64+4)return false;
    const auto place=path.substr(prefix.size(),64),image=path.substr(prefix.size()+65,64);
    return path[prefix.size()+64]=='/'&&hostedHex64(place)&&hostedHex64(image)&&
        (original.empty()||place==original)&&(path.substr(path.size()-4)==".jpg"||path.substr(path.size()-4)==".png");
}
inline bool hostedSampleFresh(bool known,unsigned long long sampled,std::time_t now=std::time(nullptr)){
    return known&&now>0&&sampled<=static_cast<unsigned long long>(now)&&static_cast<unsigned long long>(now)-sampled<=15;
}
}
