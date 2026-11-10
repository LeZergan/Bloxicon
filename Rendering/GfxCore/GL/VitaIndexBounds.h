// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace RBX { namespace Graphics {
// GPU index values, rather than the caller's range hint, determine addresses.
// Cache complete blocks; scan partial boundary blocks without including spare
// capacity that was not submitted. All access stays on the render thread.
class VitaIndexBounds {
    struct Block { uint32_t maximum=0; bool valid=false; };
    static const size_t blockSize=256;
    const void* storage=nullptr;
    size_t elements=0, width=0;
    uint64_t revision=0;
    size_t lastOffset=0,lastCount=0;
    uint32_t lastMaximum=0;
    bool lastValid=false;
    std::vector<Block> blocks;

    static uint32_t maximum(const unsigned char* data,size_t count,size_t width) {
        uint32_t result=0;
        for(size_t i=0;i<count;++i) {
            uint32_t value=0;
            if(width==2){uint16_t small;std::memcpy(&small,data+i*width,2);value=small;}
            else std::memcpy(&value,data+i*width,4);
            result=std::max(result,value);
        }
        return result;
    }
public:
    bool fits(const void* data,size_t elementWidth,size_t elementCount,uint64_t version,
        size_t offset,size_t count,size_t vertexCount) {
        if(!count)return true;
        if(!data||(elementWidth!=2&&elementWidth!=4)||offset>elementCount||count>elementCount-offset||
            elementCount>size_t(-1)/elementWidth||!vertexCount)return false;
        if(data!=storage||elementWidth!=width||elementCount!=elements||version!=revision) {
            storage=data;width=elementWidth;elements=elementCount;revision=version;
            lastValid=false;
            const size_t needed=elements/blockSize+(elements%blockSize!=0);
            try {blocks.resize(needed);for(auto& block:blocks)block.valid=false;}
            catch(const std::bad_alloc&) {blocks.clear();}
        }
        if(lastValid&&lastOffset==offset&&lastCount==count)return lastMaximum<vertexCount;
        const auto bytes=static_cast<const unsigned char*>(data);
        const size_t end=offset+count;
        uint32_t allMaximum=0;
        while(offset<end) {
            const size_t n=std::min(end-offset,blockSize-offset%blockSize);
            uint32_t value;
            const size_t block=offset/blockSize;
            if(n==blockSize&&block<blocks.size()) {
                auto& cached=blocks[block];
                if(!cached.valid){cached.maximum=maximum(bytes+offset*width,n,width);cached.valid=true;}
                value=cached.maximum;
            } else value=maximum(bytes+offset*width,n,width);
            if(value>=vertexCount)return false;
            allMaximum=std::max(allMaximum,value);
            offset+=n;
        }
        lastOffset=end-count;lastCount=count;lastMaximum=allMaximum;lastValid=true;
        return true;
    }
};
} }
