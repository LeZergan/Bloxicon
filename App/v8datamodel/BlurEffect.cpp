#include "stdafx.h"
#include "v8datamodel/BlurEffect.h"
#include <cmath>
namespace RBX {
const char* const sPostEffect = "PostEffect";
const char* const sBlurEffect = "BlurEffect";
REFLECTION_BEGIN();
const Reflection::PropDescriptor<PostEffect, bool> PostEffect::propEnabled("Enabled", category_Data, &PostEffect::getEnabled, &PostEffect::setEnabled);
const Reflection::PropDescriptor<BlurEffect, float> BlurEffect::propSize("Size", category_Data, &BlurEffect::getSize, &BlurEffect::setSize);
REFLECTION_END();
void PostEffect::setEnabled(bool value) { if(enabled!=value) { enabled=value;raisePropertyChanged(propEnabled); } }
void BlurEffect::setSize(float value) {
    if(!std::isfinite(value)) return;
    value=std::max(0.f,std::min(56.f,value));
    if(size!=value) { size=value;raisePropertyChanged(propSize); }
}
}
