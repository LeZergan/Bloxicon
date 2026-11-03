#pragma once
#include "v8datamodel/GuiObject.h"

namespace RBX {
struct TweenInfo {
    double time,delayTime;
    GuiObject::TweenEasingStyle easingStyle;
    GuiObject::TweenEasingDirection easingDirection;
    int repeatCount;
    bool reverses;
    TweenInfo(double time=1,GuiObject::TweenEasingStyle style=GuiObject::EASING_STYLE_QUAD,
        GuiObject::TweenEasingDirection direction=GuiObject::EASING_DIRECTION_OUT,
        int repeats=0,bool reverses=false,double delay=0);
    bool operator==(const TweenInfo& v) const {return time==v.time&&delayTime==v.delayTime&&easingStyle==v.easingStyle&&easingDirection==v.easingDirection&&repeatCount==v.repeatCount&&reverses==v.reverses;}
    bool operator!=(const TweenInfo& v) const {return !(*this==v);}
};
}
