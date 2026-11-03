#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <sstream>

namespace VitaBrowser {
inline unsigned supportedFrameCap(unsigned cap) {
    return cap==20?20:cap>30||cap==0?60:30;
}
inline unsigned displayVblanks(unsigned cap) { return 60/supportedFrameCap(cap); }
struct UserPreferences {
    // Session-derived display identity; never persisted over the offline name.
    std::string accountUsername;
    bool darkMode = true, bloxiconMode = false, acceptCircle = false;
    unsigned shoulderMode = 0, rearTouchMode = 0;
    unsigned fpsCap = 30, renderDistance = 0;
    unsigned gameVolume = 100, musicVolume = 35;
    bool buttonSounds = true;
    unsigned buttonVolume = 50;
    bool menuMusic = true, musicInGame = false, musicLoop = true;
    unsigned cameraSensitivity = 100, stickDeadzone = 18;
    bool invertCameraY = false, showFps = false, showChatButton = true;
    unsigned fpsPosition = 0;
    unsigned textureQuality = 256;
    bool postEffects = true;
    unsigned infoLookupMode=0,infoTitleMode=0,infoRefreshMode=0;
    bool downloadGameInfo=true,downloadIcons=true,downloadPhotos=true;
};
template<class S>const std::string& playerName(const S& settings) {
    return settings.accountUsername.empty()?settings.username:settings.accountUsername;
}
template<class S>bool playerNameEditable(const S& settings) { return settings.accountUsername.empty(); }
inline bool preferenceValue(UserPreferences& s, const std::string& key, const std::string& value) {
    char* end = nullptr;
    const long n = std::strtol(value.c_str(), &end, 10);
    if(value.empty() || *end) return false;
    auto clamp = [&](long low, long high) { return unsigned(std::max(low, std::min(high, n))); };
    if(key=="dark_mode") s.darkMode=n!=0;
    else if(key=="bloxicon_mode") s.bloxiconMode=n!=0;
    else if(key=="accept_circle") s.acceptCircle=n!=0;
    else if(key=="shoulder_mode") s.shoulderMode=clamp(0,3);
    else if(key=="rear_touch_mode") s.rearTouchMode=clamp(0,3);
    else if(key=="fps_cap") {
        const unsigned caps[]={0,20,30,40,60,90,120};
        if(std::find(std::begin(caps),std::end(caps),unsigned(n))==std::end(caps))return false;
        s.fpsCap=supportedFrameCap(unsigned(n));
    }
    else if(key=="render_distance") s.renderDistance=n==0?0:clamp(200,800);
    else if(key=="game_volume") s.gameVolume=clamp(0,100);
    else if(key=="button_sounds") s.buttonSounds=n!=0;
    else if(key=="button_volume") s.buttonVolume=clamp(0,100);
    else if(key=="music_volume") s.musicVolume=clamp(0,100);
    else if(key=="menu_music") s.menuMusic=n!=0;
    else if(key=="music_in_game") s.musicInGame=n!=0;
    else if(key=="music_loop") s.musicLoop=n!=0;
    else if(key=="camera_sensitivity") s.cameraSensitivity=clamp(25,200);
    else if(key=="stick_deadzone") s.stickDeadzone=clamp(5,35);
    else if(key=="invert_camera_y") s.invertCameraY=n!=0;
    else if(key=="show_fps") s.showFps=n!=0;
    else if(key=="fps_position") s.fpsPosition=clamp(0,4);
    else if(key=="show_chat_button") s.showChatButton=n!=0;
    else if(key=="texture_quality") s.textureQuality=n<=256?256:512;
    else if(key=="post_effects") s.postEffects=n!=0;
    else if(key=="info_lookup_mode") s.infoLookupMode=clamp(0,3);
    else if(key=="info_title_mode") s.infoTitleMode=clamp(0,1);
    else if(key=="info_refresh_mode") s.infoRefreshMode=clamp(0,1);
    else if(key=="download_game_info") s.downloadGameInfo=n!=0;
    else if(key=="download_icons") s.downloadIcons=n!=0;
    else if(key=="download_photos") s.downloadPhotos=n!=0;
    else return false;
    return true;
}
inline void writePreferences(std::ostream& out, const UserPreferences& s, bool placeOnly=false) {
    if(!placeOnly) out << "dark_mode="<<s.darkMode<<'\n'<<"bloxicon_mode="<<s.bloxiconMode<<'\n'<<"accept_circle="<<s.acceptCircle<<'\n'
        <<"menu_music="<<s.menuMusic<<'\n'<<"music_volume="<<s.musicVolume<<'\n'<<"music_loop="<<s.musicLoop<<'\n'
        <<"button_sounds="<<s.buttonSounds<<'\n'<<"button_volume="<<s.buttonVolume<<'\n'
        <<"show_chat_button="<<s.showChatButton<<'\n';
    out <<"shoulder_mode="<<s.shoulderMode<<'\n'<<"rear_touch_mode="<<s.rearTouchMode<<'\n'
        <<"fps_cap="<<s.fpsCap<<'\n'<<"render_distance="<<s.renderDistance<<'\n'<<"game_volume="<<s.gameVolume<<'\n'
        <<"music_in_game="<<s.musicInGame<<'\n'<<"camera_sensitivity="<<s.cameraSensitivity<<'\n'
        <<"stick_deadzone="<<s.stickDeadzone<<'\n'<<"invert_camera_y="<<s.invertCameraY<<'\n'
        <<"show_fps="<<s.showFps<<'\n'<<"fps_position="<<s.fpsPosition<<'\n'<<"texture_quality="<<s.textureQuality<<'\n'<<"post_effects="<<s.postEffects<<'\n';
}
inline uint64_t frameDelay(uint64_t elapsed, unsigned cap) {
    if(!cap)return 0;
    const uint64_t target=(1000000ULL+cap-1)/cap;
    return elapsed<target?target-elapsed:0;
}
inline std::string placeSettingsKey(const std::string& path) {
    uint64_t hash=14695981039346656037ULL;
    for(unsigned char c:path) { hash^=c; hash*=1099511628211ULL; }
    std::ostringstream out;out<<std::hex<<hash;return out.str();
}
}
