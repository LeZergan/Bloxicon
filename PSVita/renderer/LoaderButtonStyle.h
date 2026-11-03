#pragma once
#include "LoaderDesign.h"

namespace VitaBrowser { namespace Design {
struct ButtonPaint {unsigned top,bottom,edge;unsigned char outerShadow,innerShadow,gloss;};
inline ButtonPaint buttonPaint(bool selected,bool enabled=true) {
    if(!enabled)return {subtle,surface,line,8,14,4};
    if(bloxiconMode) {
        if(darkMode)return selected?ButtonPaint{0x743039,0x3e1721,0xba626b,20,38,24}:
            ButtonPaint{0x4a2a32,0x21181e,0x80434c,18,32,18};
        return selected?ButtonPaint{0xffded8,0xe9b2ad,0xc7787e,14,24,48}:
            ButtonPaint{0xfff5f1,0xf2c9c4,0xd6a0a0,12,22,40};
    }
    if(darkMode)return selected?ButtonPaint{0x2c526a,0x183545,0x5b91ad,20,38,20}:
        ButtonPaint{0x263e50,0x17222f,0x526777,18,32,14};
    return selected?ButtonPaint{0xe6f5ff,0xb6d6eb,0x6e9cb6,14,24,48}:
        ButtonPaint{0xf4faff,0xd6e4ef,0xb4c6d4,12,22,40};
}
} }
