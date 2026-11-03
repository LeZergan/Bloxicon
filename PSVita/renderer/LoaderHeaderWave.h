#pragma once

#include <algorithm>
#include <cmath>

namespace VitaBrowser { namespace HeaderWave {
struct Point { float x, y; };

// The three paths from the website's console-header-wave SVG. Sample once;
// only the horizontal transform changes as the loader redraws.
struct Paths {
    static constexpr unsigned segments = 24;
    Point samples[3][2 * segments + 1];
    Paths() {
        const Point controls[3][7] = {
            {{-80,35},{280,200},{500,-65},{850,40},{1200,145},{1280,180},{1680,10}},
            {{-80,62},{280,190},{500,-24},{850,76},{1200,176},{1280,215},{1680,68}},
            {{-80,120},{290,65},{550,160},{930,60},{1310,-40},{1340,70},{1680,120}}
        };
        for (unsigned wave=0; wave<3; ++wave)
            for (unsigned i=0; i<=2*segments; ++i) {
                const unsigned half=i>segments ? 1 : 0;
                const Point* p=controls[wave]+half*3;
                const float t=float(i-half*segments)/segments, s=1-t;
                samples[wave][i]={
                    s*s*s*p[0].x+3*s*s*t*p[1].x+3*s*t*t*p[2].x+t*t*t*p[3].x,
                    s*s*s*p[0].y+3*s*s*t*p[1].y+3*s*t*t*p[2].y+t*t*t*p[3].y};
            }
    }
    float y(unsigned wave,float x,unsigned& i) const {
        const auto& path=samples[wave];
        while(i+1<2*segments && path[i+1].x<x) ++i;
        const float t=std::max(0.f,std::min(1.f,(x-path[i].x)/(path[i+1].x-path[i].x)));
        return path[i].y+(path[i+1].y-path[i].y)*t;
    }
};

inline float phase(double seconds) {
    const double elapsed=std::fmod(seconds,360.0);
    const double x=(elapsed<=180.0 ? elapsed : 360.0-elapsed)/180.0;
    // CSS ease-in-out: cubic-bezier(.42, 0, .58, 1).
    double lo=0,hi=1;
    for(unsigned i=0;i<14;++i) {
        const double t=(lo+hi)*.5,s=1-t;
        const double bx=3*s*s*t*.42+3*s*t*t*.58+t*t*t;
        if(bx<x)lo=t;else hi=t;
    }
    const double t=(lo+hi)*.5;
    return float(t*t*(3-2*t));
}

inline float sourceX(float position,float drift) {
    const float translation=(-.02f+.04f*drift)*1.06f;
    const float scale=(1.04f-.03f*drift)*1.06f;
    return 800.f+1600.f*(position-.5f-translation)/scale;
}

inline float visibleY(float svgY) {
    return std::max(0.f,std::min(1.f,-.12f+1.24f*svgY/160.f));
}
}}
