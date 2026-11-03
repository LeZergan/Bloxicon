#pragma once

#include "v8datamodel/BlurEffect.h"
#include "g3d/Color3.h"

namespace RBX {
extern const char* const sColorCorrectionEffect;

class ColorCorrectionEffect : public DescribedCreatable<ColorCorrectionEffect, PostEffect, sColorCorrectionEffect> {
    float brightness;
    float contrast;
    float saturation;
    G3D::Color3 tintColor;
public:
    ColorCorrectionEffect() : DescribedCreatable("ColorCorrectionEffect"), brightness(0), contrast(0), saturation(0), tintColor(1, 1, 1) {}
    float getBrightness() const { return brightness; }
    float getContrast() const { return contrast; }
    float getSaturation() const { return saturation; }
    G3D::Color3 getTintColor() const { return tintColor; }
    void setBrightness(float value);
    void setContrast(float value);
    void setSaturation(float value);
    void setTintColor(G3D::Color3 value);
    bool isIdentity() const { return brightness == 0 && contrast == 0 && saturation == 0 && tintColor == G3D::Color3(1, 1, 1); }
    static const Reflection::PropDescriptor<ColorCorrectionEffect, float> propBrightness;
    static const Reflection::PropDescriptor<ColorCorrectionEffect, float> propContrast;
    static const Reflection::PropDescriptor<ColorCorrectionEffect, float> propSaturation;
    static const Reflection::PropDescriptor<ColorCorrectionEffect, G3D::Color3> propTintColor;
};
}
