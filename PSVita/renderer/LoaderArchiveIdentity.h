#pragma once
#include <boost/uuid/detail/sha1.hpp>
#include <atomic>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <filesystem>
#endif

namespace VitaBrowser { namespace ArchiveIdentity {
inline bool validHash(const std::string& hash){
    if(hash.size()!=40)return false;
    for(unsigned char c:hash)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
// Read the original bytes on a worker. Git object framing identifies an exact
// archived file even after it is renamed; no filter, conversion or write runs.
inline std::string fileHash(const std::string& path,unsigned long long size,const std::atomic<bool>& cancelled){
    if(cancelled.load(std::memory_order_acquire)||!size||size>128ull*1024*1024)return {};
#if defined(_WIN32)
    std::ifstream input(std::filesystem::u8path(path),std::ios::binary);
#else
    std::ifstream input(path.c_str(),std::ios::binary);
#endif
    if(!input)return {};
    boost::uuids::detail::sha1 hash;const std::string header="blob "+std::to_string(size)+'\0';
    // Vita I/O workers have a 32 KiB stack. Keep the 64 KiB streaming window
    // on the heap: a local array reserves its whole frame before even opening
    // the file, overflowing those workers during original-byte identity lookup.
    hash.process_bytes(header.data(),header.size());std::vector<char> buffer(65536);unsigned long long read=0;
    while(input){
        if(cancelled.load(std::memory_order_acquire))return {};
        input.read(buffer.data(),buffer.size());const size_t bytes=size_t(input.gcount());
        if(bytes>size-read)return {};
        hash.process_bytes(buffer.data(),bytes);read+=bytes;
    }
    if(!input.eof()||read!=size||cancelled.load(std::memory_order_acquire))return {};
    boost::uuids::detail::sha1::digest_type digest;hash.get_digest(digest);std::ostringstream text;
    text<<std::hex<<std::setfill('0');for(unsigned word:digest)text<<std::setw(8)<<word;
    return text.str();
}
struct Match {size_t index=size_t(-1),count=0;bool ambiguous=false;};
template<class Item>
Match match(const std::vector<Item>& items,const std::string& hash,unsigned long long bytes){
    Match result;if(!validHash(hash)||!bytes)return result;unsigned long long id=0;
    for(size_t i=0;i<items.size();++i){const auto& item=items[i];
        if(item.contentHash!=hash||item.size!=bytes)continue;
        ++result.count;
        if(item.placeId&&id&&item.placeId!=id)result.ambiguous=true;
        if(result.index==size_t(-1)||(!id&&item.placeId))result.index=i;
        if(item.placeId)id=item.placeId;
    }
    if(result.ambiguous)result.index=size_t(-1);
    return result;
}
} }
