#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace VitaAvatar {
class PreviewCamera
{
    std::array<float, 3> center_ = {{0, 3, 0}}, target_ = {{0, 3, 0}};
    float radius_ = 3.5f, targetRadius_ = 3.5f, yaw_ = .28f;
    float zoom_ = 1.f, targetZoom_ = 1.f, homeZoom_ = 1.f;
    float halfHeight_ = 3.f, targetHalfHeight_ = 3.f;
    float composition_ = 0.f, targetComposition_ = 0.f;
    std::array<float,3> subject_ = {{0,3,0}}, targetSubject_ = {{0,3,0}};
    float subjectBlend_ = 0.f;
    bool hasSubject_ = false;
    bool framed_ = false;
    float aspect_ = 1.f;
public:
    static constexpr float fieldOfView = 35.f;
    static constexpr float minimumZoom = .45f, maximumZoom = 1.7f;
    void setAspectRatio(float value){if(std::isfinite(value)&&value>.1f&&value<10.f)aspect_=value;}
    void setSubject(const std::array<float,3>& point)
    {
        for(float value:point) if(!std::isfinite(value) || std::fabs(value)>100000.f) return;
        if(!hasSubject_) subject_=point;
        targetSubject_=point; hasSubject_=true;
    }
    void clearSubject() { hasSubject_=false; }
    void setView(float composition, float homeZoom)
    {
        setComposition(composition);
        if(!std::isfinite(homeZoom)) return;
        homeZoom_=std::max(minimumZoom,std::min(maximumZoom,homeZoom));
        targetZoom_=homeZoom_;
    }
    void setComposition(float bias)
    {
        if(!std::isfinite(bias)) return;
        targetComposition_=std::max(-.35f,std::min(.65f,bias));
    }
    void reset()
    {
        yaw_ = .28f; zoom_ = targetZoom_ = homeZoom_;
        composition_=targetComposition_;
        subject_=targetSubject_; subjectBlend_=hasSubject_?1.f:0.f;
        if(framed_) { center_=target_; radius_=targetRadius_; halfHeight_=targetHalfHeight_; }
    }
    void clear()
    {
        framed_=false;
        composition_=targetComposition_=0.f;
        hasSubject_=false;subjectBlend_=0.f;
        homeZoom_=targetZoom_=zoom_=1.f;
        reset();
    }
    bool bounds(const std::array<float,3>& low, const std::array<float,3>& high)
    {
        float squared = 0;
        for (unsigned i=0; i<3; ++i)
        {
            if (!std::isfinite(low[i]) || !std::isfinite(high[i]) || low[i]>high[i] ||
                std::fabs(low[i])>100000.f || std::fabs(high[i])>100000.f) return false;
            const float half = (high[i]-low[i])*.5f;
            squared += half*half;
        }
        const float radius = std::sqrt(squared);
        if (!std::isfinite(radius) || radius>128.f) return false;
        for (unsigned i=0; i<3; ++i) target_[i] = (low[i]+high[i])*.5f;
        targetRadius_ = std::max(1.f,radius);
        targetHalfHeight_ = (high[1]-low[1])*.5f;
        if (!framed_)
        { center_=target_; radius_=targetRadius_; halfHeight_=targetHalfHeight_; framed_=true; }
        // A late accessory can move the bounds center as well as enlarge them.
        // Enclose the new sphere around the current eased center; easing both
        // center and radius by the same factor keeps full-body framing safe.
        float offsetSquared=0;
        for(unsigned i=0;i<3;++i) {const float delta=center_[i]-target_[i];offsetSquared+=delta*delta;}
        radius_=std::max(radius_,targetRadius_+std::sqrt(offsetSquared));
        return true;
    }
    void step(float dt, float orbit, float zoom)
    {
        if (!std::isfinite(dt)) dt=0;
        dt=std::max(0.f,std::min(.1f,dt));
        if (!std::isfinite(orbit)) orbit=0;
        if (!std::isfinite(zoom)) zoom=0;
        yaw_=std::remainder(yaw_+std::max(-1.f,std::min(1.f,orbit))*dt*2.f,6.283185307f);
        targetZoom_=std::max(minimumZoom,std::min(maximumZoom,
            targetZoom_+std::max(-1.f,std::min(1.f,zoom))*dt*.65f));
        const float blend=1.f-std::exp(-8.f*dt);
        for (unsigned i=0; i<3; ++i) center_[i]+=(target_[i]-center_[i])*blend;
        radius_+=(targetRadius_-radius_)*blend;
        halfHeight_+=(targetHalfHeight_-halfHeight_)*blend;
        composition_+=(targetComposition_-composition_)*blend;
        zoom_+=(targetZoom_-zoom_)*blend;
        for(unsigned i=0;i<3;++i) subject_[i]+=(targetSubject_[i]-subject_[i])*blend;
        subjectBlend_+=((hasSubject_?1.f:0.f)-subjectBlend_)*blend;
    }
    std::array<float,3> target() const
    {
        auto p=center_;
        p[1]+=halfHeight_*composition_;
        // Category composition is applied once, not multiplied by close zoom.
        // Native category views anchor to the actual body part below, so a tall
        // hat or asymmetric package cannot move the face above the frame.
        const float inspectionBias=std::fabs(targetComposition_)<.01f?.55f:0.f;
        p[1]+=halfHeight_*inspectionBias*std::max(0.f,1.f-zoom_)/(1.f-minimumZoom);
        for(unsigned i=0;i<3;++i) p[i]+=(subject_[i]-p[i])*subjectBlend_;
        return p;
    }
    float distance() const
    {
        const float vertical=fieldOfView*3.141592654f/360.f;
        const float halfAngle=std::atan(std::tan(vertical)*std::min(1.f,aspect_));
        return std::max(radius_*1.25f, radius_*1.08f*zoom_/std::sin(halfAngle));
    }
    std::array<float,3> position() const
    {
        auto p=target();
        const float d=distance();
        p[0]+=std::sin(yaw_)*d; p[1]+=radius_*.08f; p[2]-=std::cos(yaw_)*d;
        return p;
    }
    float radius() const { return radius_; }
    float zoom() const { return zoom_; }
};
} // namespace VitaAvatar
