#pragma once
#include <cstdint>
namespace VitaAvatar {
// A pending worker or unrelated shared-renderer texture never owns the editor
// forever. Callers retain the animated rig and report incomplete loading.
class LoadGuard {
    uint64_t verificationStarted_=0, texturesStarted_=0;
    bool verifying_=false, textures_=false;
public:
    void begin(uint64_t now) {verificationStarted_=now;verifying_=true;textures_=false;}
    void complete() {verifying_=false;}
    bool verificationExpired(uint64_t now) const {
        return verifying_ && now-verificationStarted_>=15000000;
    }
    enum TextureState {Ready, Loading, Incomplete};
    TextureState textures(unsigned queued,uint64_t now) {
        if(!queued){textures_=false;return Ready;}
        if(!textures_){textures_=true;texturesStarted_=now;}
        return now-texturesStarted_>=8000000 ? Incomplete : Loading;
    }
};
}
