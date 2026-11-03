#pragma once
#include "LoaderPreferences.h"
#include <fstream>
namespace VitaBrowser {
struct InfoLookupPolicy {
    enum Method { IdAndTitle,IdsOnly,TitleOnly,SavedOnly };
    unsigned method=IdAndTitle;
    bool automaticExactTitle=false,alwaysRefresh=false;
    bool info=true,icons=true,photos=true;
    bool network()const{return method!=SavedOnly&&(info||icons||photos);}
    bool titleSearch()const{return false;}
    bool operator==(const InfoLookupPolicy& p)const{return method==p.method&&automaticExactTitle==p.automaticExactTitle&&alwaysRefresh==p.alwaysRefresh&&info==p.info&&icons==p.icons&&photos==p.photos;}
    bool operator!=(const InfoLookupPolicy& p)const{return !(*this==p);}
    static InfoLookupPolicy forPlace(const InfoLookupPolicy& base,const std::string& path){
        if(path.empty())return base;
        std::ifstream input("ux0:/data/RBXV00001/system/place-settings/"+placeSettingsKey(path)+".cfg");
        std::string line;if(!std::getline(input,line)||line!="place="+path)return base;
        UserPreferences settings;settings.infoLookupMode=base.method;settings.infoTitleMode=base.automaticExactTitle;
        settings.infoRefreshMode=base.alwaysRefresh;settings.downloadGameInfo=base.info;settings.downloadIcons=base.icons;settings.downloadPhotos=base.photos;
        size_t bytes=line.size();while(std::getline(input,line)){
            if((bytes+=line.size()+1)>8192)return base;
            const auto equals=line.find('=');if(equals==std::string::npos)continue;
            preferenceValue(settings,line.substr(0,equals),line.substr(equals+1));
        }
        return from(settings);
    }
    template<class S>static InfoLookupPolicy from(const S& settings){
        (void)settings;
        InfoLookupPolicy p;p.method=IdsOnly;p.automaticExactTitle=false;
        p.alwaysRefresh=false;p.info=true;p.icons=true;p.photos=false;return p;
    }
};
}
