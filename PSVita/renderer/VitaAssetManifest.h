#pragma once
#include <cstdint>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "VitaPlaceTransfer.h"

namespace VitaAdhoc { namespace Transfer {
static const uint64_t maxManifestBytes=256*1024;
static const uint64_t maxAssetBytes=16*1024*1024;
static const uint64_t maxAssetTotalBytes=64*1024*1024;
static const size_t maxAssets=512;
struct AssetEntry {
    std::string name,sha256;
    uint64_t bytes=0,fingerprint=0;
    uint32_t file=0;
};
inline bool decimal64(const std::string& text,uint64_t& value) {
    if(text.empty() || text.size()>20 || (text.size()>1 && text[0]=='0'))return false;
    value=0;
    for(unsigned char c:text) {
        if(c<'0'||c>'9'||value>(std::numeric_limits<uint64_t>::max()-(c-'0'))/10)return false;
        value=value*10+(c-'0');
    }
    return true;
}
inline bool numericAsset(const std::string& name) {
    if(name.empty()||name.size()>20||name[0]=='0')return false;
    for(unsigned char c:name)if(c<'0'||c>'9')return false;
    return true;
}
inline bool sha256Text(const std::string& value) {
    if(value.size()!=64)return false;
    for(unsigned char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
// Build into a temporary vector. Invalid trailing rows never publish entries.
inline bool parseAssetManifest(const std::string& text,bool account,
                              std::vector<AssetEntry>& result) {
    if(text.size()>maxManifestBytes)return false;
    std::vector<AssetEntry> parsed;
    std::set<std::string> seen;
    uint64_t total=0;
    std::istringstream input(text);std::string line;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()||line.size()>512||parsed.size()>=maxAssets)return false;
        for(unsigned char c:line)if(c!=9&&(c<32||c>126))return false;
        AssetEntry entry;std::string bytes,hash,extra;
        std::istringstream row(line);
        if(!(row>>entry.name>>bytes>>hash))return false;
        if(row>>entry.sha256) {if(!sha256Text(entry.sha256)||row>>extra)return false;}
        if((account?(!numericAsset(entry.name)||entry.sha256.empty()):!safeAssetName(entry.name))||
           !seen.insert(entry.name).second||!decimal64(bytes,entry.bytes)||
           !decimal64(hash,entry.fingerprint)||!entry.bytes||entry.bytes>maxAssetBytes||
           entry.bytes>maxAssetTotalBytes-total)return false;
        total+=entry.bytes;entry.file=static_cast<uint32_t>(parsed.size()+2);
        parsed.push_back(std::move(entry));
    }
    if(input.bad())return false;
    result.swap(parsed);return true;
}
} }
