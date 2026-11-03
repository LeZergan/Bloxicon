#include "stdafx.h"
#include "v8datamodel/ColorCorrectionEffect.h"
#include <cmath>

namespace RBX {
const char* const sColorCorrectionEffect = "ColorCorrectionEffect";
REFLECTION_BEGIN();
const Reflection::PropDescriptor<ColorCorrectionEffect, float> ColorCorrectionEffect::propBrightness("Brightness", category_Data, &ColorCorrectionEffect::getBrightness, &ColorCorrectionEffect::setBrightness);
const Reflection::PropDescriptor<ColorCorrectionEffect, float> ColorCorrectionEffect::propContrast("Contrast", category_Data, &ColorCorrectionEffect::getContrast, &ColorCorrectionEffect::setContrast);
const Reflection::PropDescriptor<ColorCorrectionEffect, float> ColorCorrectionEffect::propSaturation("Saturation", category_Data, &ColorCorrectionEffect::getSaturation, &ColorCorrectionEffect::setSaturation);
const Reflection::PropDescriptor<ColorCorrectionEffect, G3D::Color3> ColorCorrectionEffect::propTintColor("TintColor", category_Data, &ColorCorrectionEffect::getTintColor, &ColorCorrectionEffect::setTintColor);
REFLECTION_END();

void ColorCorrectionEffect::setBrightness(float value) {
    if (std::isfinite(value) && brightness != value) { brightness = value; raisePropertyChanged(propBrightness); }
}
void ColorCorrectionEffect::setContrast(float value) {
    if (std::isfinite(value) && contrast != value) { contrast = value; raisePropertyChanged(propContrast); }
}
void ColorCorrectionEffect::setSaturation(float value) {
    if (std::isfinite(value) && saturation != value) { saturation = value; raisePropertyChanged(propSaturation); }
}
void ColorCorrectionEffect::setTintColor(G3D::Color3 value) {
    if (std::isfinite(value.r) && std::isfinite(value.g) && std::isfinite(value.b) && tintColor != value) {
        tintColor = value; raisePropertyChanged(propTintColor);
    }
}
}
