#pragma once
#include "LoaderDesign.h"
#include <string>
#include <vector>

namespace VitaBrowser { namespace Gallery {
constexpr size_t limit=4,visible=3;
struct Photo {long long id=0;std::string url;};
struct Manifest {long long universeId=0;std::vector<Photo> photos;bool ready=false,pending=false;};
inline size_t pageBase(size_t selected,size_t count){return std::min(selected/visible*visible,count>visible?count-visible:0);}
inline Design::Rect row(size_t slot){return {616,156+float(slot)*76,320,68};}
inline int hit(float x,float y,size_t selected,size_t count){const auto first=pageBase(selected,count);
    for(size_t i=0;i<visible&&first+i<count;++i)if(row(i).contains(x,y))return int(first+i);
    return -1;
}
inline size_t move(size_t selected,size_t count,int direction){
    if(!count)return 0;
    selected=std::min(selected,count-1);
    return direction<0?(selected?selected-1:0):std::min(selected+1,count-1);
}
} }
