#pragma once
#include "V8Tree/Instance.h"
#include "reflection/Property.h"
namespace RBX {
extern const char* const sPostEffect;
extern const char* const sBlurEffect;
class PostEffect : public DescribedCreatable<PostEffect, Instance, sPostEffect, Reflection::ClassDescriptor::INTERNAL> {
    bool enabled;
public:
    explicit PostEffect(const char* name = "PostEffect") : DescribedCreatable(name), enabled(true) {}
    bool getEnabled() const { return enabled; }
    void setEnabled(bool value);
    static const Reflection::PropDescriptor<PostEffect, bool> propEnabled;
};
class BlurEffect : public DescribedCreatable<BlurEffect, PostEffect, sBlurEffect> {
    float size;
public:
    BlurEffect() : DescribedCreatable("BlurEffect"), size(24) {}
    float getSize() const { return size; }
    void setSize(float value);
    static const Reflection::PropDescriptor<BlurEffect, float> propSize;
};
}
