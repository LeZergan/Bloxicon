#pragma once
#include "LoaderDesign.h"
#include "LoaderGameInfo.h"

namespace VitaBrowser { namespace GameDetails {
enum Tab {Overview,Description,Storage,TabCount};
constexpr size_t descriptionRows=8;
struct DisplayInfo {
    std::array<std::string,GameInfo::CounterCount> counts;
    std::string created,updated,genre,snapshot;
    void update(const GameInfo& info){for(unsigned i=0;i<GameInfo::CounterCount;++i)counts[i]=info.value(i);
        created=info.created.empty()?"Unavailable":info.created;updated=info.updated.empty()?"Unavailable":info.updated;
        genre="Genre / "+(info.genre.empty()?std::string("Not supplied"):info.genre);snapshot=info.snapshot();}
};
inline const char* tabName(unsigned tab){const char* names[]={"Overview","Description","Files"};return names[std::min(tab,unsigned(TabCount-1))];}
inline Design::Rect tabRect(unsigned tab){return {340+float(tab)*200,72,192,44};}
inline int tabHit(float x,float y){for(unsigned i=0;i<TabCount;++i)if(tabRect(i).contains(x,y))return int(i);return -1;}
inline size_t pages(size_t lines){return std::max(size_t(1),(lines+descriptionRows-1)/descriptionRows);}
} }
