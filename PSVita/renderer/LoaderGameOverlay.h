#pragma once
#include "LoaderDesign.h"

namespace VitaBrowser { namespace GameOverlay {
// Leave the original top-bar chat control visible and reachable.
inline Design::Rect chatButton(){return {0,0,0,0};}
// Keep the optional counter clear of the stock chat and player-list corners.
inline Design::Rect fpsBadge(unsigned position=0){
    switch(position){
    case 1:return {12,52,132,30};
    case 2:return {816,8,132,30};
    case 3:return {12,498,132,30};
    case 4:return {816,498,132,30};
    default:return {414,8,132,30};
    }
}
// Latch a touch until every finger is lifted. Moving into the button or
// returning from the text dialog with a held finger cannot reopen it.
class ChatTouch {
    bool touching=false;
public:
    bool sample(bool anyTouch,bool startsInside,bool enabled){
        const bool activate=enabled&&anyTouch&&!touching&&startsInside;
        touching=anyTouch;return activate;
    }
};
} }
