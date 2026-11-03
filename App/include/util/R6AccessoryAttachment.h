#pragma once
#include <string>
namespace RBX { namespace R6AccessoryAttachment {
enum Body {Head,Torso,LeftArm,RightArm};
struct Point {Body body;float x=0,y=0,z=0;};
// Standard R6 attachment locations, expressed relative to part half extents.
// These are invisible rig metadata. Meshes and their authored handle frames
// stay unchanged; existing body attachments always take precedence.
inline bool point(const std::string& name,Point& result) {
    if(name=="HatAttachment"||name=="HairAttachment"){result={Head,0,1.2f,0};return true;}
    if(name=="FaceFrontAttachment"){result={Head,0,0,-1.2f};return true;}
    if(name=="FaceCenterAttachment"){result={Head,0,0,0};return true;}
    if(name=="NeckAttachment"){result={Torso,0,1,0};return true;}
    if(name=="BodyFrontAttachment"){result={Torso,0,0,-1};return true;}
    if(name=="BodyBackAttachment"){result={Torso,0,0,1};return true;}
    if(name=="LeftCollarAttachment"){result={Torso,-1,1,0};return true;}
    if(name=="RightCollarAttachment"){result={Torso,1,1,0};return true;}
    if(name=="WaistCenterAttachment"){result={Torso,0,-1,0};return true;}
    if(name=="WaistFrontAttachment"){result={Torso,0,-1,-1};return true;}
    if(name=="WaistBackAttachment"){result={Torso,0,-1,1};return true;}
    if(name=="LeftShoulderAttachment"){result={LeftArm,0,1,0};return true;}
    if(name=="RightShoulderAttachment"){result={RightArm,0,1,0};return true;}
    return false;
}
} }
