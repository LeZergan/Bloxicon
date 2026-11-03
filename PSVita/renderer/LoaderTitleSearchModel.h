#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include "LoaderDesign.h"
namespace VitaBrowser { namespace TitleSearch {
constexpr size_t limit=12,visible=4;
struct Candidate {long long placeId=0,universeId=0;std::string name,creator,description;};
struct Result {std::string query,error;std::vector<Candidate> candidates;bool truncated=false;};
inline std::string key(const std::string& text){
    std::string result;
    for(size_t i=0;i<text.size();){
        const auto start=i;unsigned char first=text[i++];unsigned code=first,extra=0;
        if(first>=0xc2&&first<=0xdf){code=first&31;extra=1;}
        else if(first>=0xe0&&first<=0xef){code=first&15;extra=2;}
        else if(first>=0xf0&&first<=0xf4){code=first&7;extra=3;}
        else if(first>=128)return {};
        if(i+extra>text.size())return {};
        for(unsigned n=0;n<extra;++n){const unsigned char next=text[i++];if((next&0xc0)!=0x80)return {};code=(code<<6)|(next&63);}
        if((extra==1&&code<128)||(extra==2&&code<2048)||(extra==3&&code<65536)||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return {};
        if(code<128){if(std::isalnum(static_cast<unsigned char>(code)))result+=char(std::tolower(static_cast<unsigned char>(code)));}
        else if(!(code>=0x1f000&&code<=0x1ffff)&&code!=0xfe0f&&code!=0xfe0e&&!(code>=0x2000&&code<=0x206f)&&!(code>=0x3000&&code<=0x303f))
            result.append(text,start,i-start);
    }
    return result;
}
inline size_t uniqueExact(const Result& result){
    if(result.truncated)return size_t(-1);
    const auto query=key(result.query);if(query.empty())return size_t(-1);
    size_t found=size_t(-1);
    for(size_t i=0;i<result.candidates.size();++i)if(key(result.candidates[i].name)==query){
        if(found!=size_t(-1)&&result.candidates[found].universeId!=result.candidates[i].universeId)return size_t(-1);
        found=i;
    }
    return found;
}
inline size_t pageBase(size_t selected,size_t count){return std::min(selected/visible*visible,count>visible?count-visible:0);}
inline Design::Rect rowRect(size_t slot,size_t count){
    const auto rows=std::max(size_t(1),std::min(visible,count));const float height=(304.f-8.f*float(rows-1))/float(rows);
    return {12,112+float(slot)*(height+8),600,height};
}
} }
