#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace VitaBrowser { namespace ProgressUI {

enum Kind {
    Install,
    Update,
    DirectUrl,
    Launch,
    Host,
    JoinNearby
};

struct Model {
    Kind kind=Launch;
    const char* eyebrow="PREPARING GAME";
    std::string title;
    const char* detail="Preparing the place and its content.";
    const char* note="Please wait. This screen will continue automatically.";
};

inline std::string safeTitle(const std::string& value)
{
    const auto visible=std::find_if(value.begin(),value.end(),[](unsigned char c) {
        return !std::isspace(c);
    });
    return visible==value.end() ? std::string("Original place") : value;
}

inline Model install(const std::string& title, bool update)
{
    Model result;
    result.kind=update ? Update : Install;
    result.eyebrow=update ? "UPDATING GAME" : "INSTALLING GAME";
    result.title=safeTitle(title);
    result.detail=update
        ? "Downloading, verifying and safely replacing the installed place."
        : "Downloading, verifying and safely installing this place.";
    result.note="Added to My Games when finished. Choose Play to start it.";
    return result;
}

inline Model directUrl()
{
    Model result;
    result.kind=DirectUrl;
    result.eyebrow="INSTALLING FROM URL";
    result.title="Original place";
    result.detail="Downloading and verifying the selected place before installation.";
    result.note="The existing install is preserved until verification succeeds.";
    return result;
}

inline Model launch(const std::string& title, Kind kind)
{
    Model result;
    result.kind=kind;
    result.title=safeTitle(title);
    if (kind==Host)
    {
        result.eyebrow="STARTING HOST";
        result.detail="Preparing the place, player data and server.";
    }
    else if (kind==JoinNearby)
    {
        result.eyebrow="FINDING SERVERS";
        result.detail="Preparing the place and opening the server list.";
    }
    else
    {
        result.eyebrow="STARTING GAME";
        result.detail="Reading the place and preparing saved data and assets.";
    }
    result.note="The game will open automatically when preparation is complete.";
    return result;
}

inline bool valid(const Model& model)
{
    return model.eyebrow && *model.eyebrow && !model.title.empty() &&
           model.detail && *model.detail && model.note && *model.note;
}

inline float activity(float value)
{
    if (!std::isfinite(value))
        return 0.f;
    value-=std::floor(value);
    return value < 0.f ? value+1.f : value;
}

inline float segmentX(float value, float left, float width, float segmentWidth)
{
    const float available=std::max(0.f,width-segmentWidth);
    return left+available*activity(value);
}

} }
