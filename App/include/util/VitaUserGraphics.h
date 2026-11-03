#pragma once
#include <atomic>
namespace RBX {
inline std::atomic<unsigned>& vitaWorldTextureSize(){static std::atomic<unsigned> size{256};return size;}
inline std::atomic<bool>& vitaPostEffects(){static std::atomic<bool> enabled{true};return enabled;}
inline bool desktopPostEffects(){
#ifdef RBX_PC_CLIENT
    return vitaPostEffects().load();
#else
    return true;
#endif
}
}
