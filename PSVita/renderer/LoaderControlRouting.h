#pragma once
#include "VitaRearTouch.h"
namespace VitaInput {
enum RoutedShoulder : unsigned { BumperL=1, BumperR=2, TriggerL=4, TriggerR=8, ClickL=16, ClickR=32 };
inline unsigned routeShoulders(bool left,bool right,unsigned rear,unsigned physicalMode,unsigned rearMode) {
    const bool leftTrigger=physicalMode==1||physicalMode==3;
    const bool rightTrigger=physicalMode==1||physicalMode==2;
    unsigned result=(left?(leftTrigger?TriggerL:BumperL):0u)|(right?(rightTrigger?TriggerR:BumperR):0u);
    if(rearMode==3)return result;
    const bool l=(rear&L2)!=0,r=(rear&R2)!=0;
    result|=(l?(rearMode==1?BumperL:rearMode==2?ClickL:TriggerL):0u)
        |(r?(rearMode==1?BumperR:rearMode==2?ClickR:TriggerR):0u);
    result|=((rear&L3)?ClickL:0u)|((rear&R3)?ClickR:0u);
    return result;
}
inline unsigned menuButtons(unsigned mask,unsigned cross,unsigned circle,bool swap) {
    if(!swap)return mask;
    return (mask&~(cross|circle))|((mask&cross)?circle:0u)|((mask&circle)?cross:0u);
}
}
