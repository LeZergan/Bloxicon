#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace VitaBrowser {
// Reuse the Details downloader's PNG IHDR policy before cached-image decoding.
// The existing 1024px UI limit is unchanged; non-PNG formats keep their original
// decoder path. This bounds allocation, not codec validity (CRC/IDAT/etc.).
inline bool loaderImagePreflight(const unsigned char* bytes, size_t count)
{
    if(count>=3&&bytes[0]==255&&bytes[1]==216&&bytes[2]==255){
        size_t at=2;
        while(at+1<count){
            if(bytes[at++]!=255)return false;
            while(at<count&&bytes[at]==255)++at;
            if(at>=count)return false;
            const unsigned marker=bytes[at++];
            if(marker==0xd8||marker==0x01||(marker>=0xd0&&marker<=0xd7))continue;
            if(marker==0xd9||marker==0xda||at+2>count)return false;
            const size_t length=(size_t(bytes[at])<<8)|bytes[at+1];
            if(length<2||length>count-at)return false;
            if((marker>=0xc0&&marker<=0xc3)||(marker>=0xc5&&marker<=0xc7)||(marker>=0xc9&&marker<=0xcb)||(marker>=0xcd&&marker<=0xcf)){
                if(length<8)return false;
                const unsigned height=(unsigned(bytes[at+3])<<8)|bytes[at+4],width=(unsigned(bytes[at+5])<<8)|bytes[at+6];
                return width>0&&height>0&&width<=1024&&height<=1024;
            }
            at+=length;
        }
        return false;
    }
    static const unsigned char png[] = {137,80,78,71,13,10,26,10};
    if (count < sizeof(png) || std::memcmp(bytes, png, sizeof(png)) != 0)
        return true;
    if (count < 24) return false;
    const auto big32 = [](const unsigned char* p) -> uint32_t {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    };
    return big32(bytes + 8) == 13 && std::memcmp(bytes + 12, "IHDR", 4) == 0 &&
           big32(bytes + 16) > 0 && big32(bytes + 16) <= 1024 &&
           big32(bytes + 20) > 0 && big32(bytes + 20) <= 1024;
}
}
